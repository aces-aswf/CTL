#include "Oracle.h"

#include <half.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace ctltest {

namespace {

// Look up the tightest tolerance that applies to a given dotted path. The
// base tolerance is assumed to already cover the root; per_field entries
// override on an exact dotted-path match (e.g. "out.r" or "return[2]").
Tolerance resolveAt(const Tolerance& base,
                    const std::string& path,
                    std::string& source /*in/out*/)
{
    auto it = base.per_field.find(path);
    if (it != base.per_field.end()) {
        source = source + ".per_field[" + path + "]";
        return Tolerance::merge(base, it->second);
    }
    return base;
}

std::string leafText(const Value& v) {
    return v.describe();
}

// Signed ULP distance between two floats, saturating at INT64_MAX. NaN/inf
// compare as non-matching to INT64_MAX.
int64_t ulpsBetween(float a, float b) {
    if (std::isnan(a) || std::isnan(b) || std::isinf(a) || std::isinf(b)) {
        if (a == b) return 0;
        return INT64_MAX;
    }
    uint32_t ai, bi;
    std::memcpy(&ai, &a, sizeof(ai));
    std::memcpy(&bi, &b, sizeof(bi));
    // Map signed-magnitude to biased so ordering matches ULP-distance.
    auto toBiased = [](uint32_t x) -> int64_t {
        if (x & 0x80000000u) return (int64_t)0x80000000LL - (int64_t)(x & 0x7fffffffu);
        return (int64_t)0x80000000LL + (int64_t)x;
    };
    int64_t ab = toBiased(ai);
    int64_t bb = toBiased(bi);
    return ab > bb ? ab - bb : bb - ab;
}

bool compareFloat(double expected, double got, const Tolerance& tol,
                  double& absErrOut, double& relErrOut, int& ulpErrOut)
{
    const double diff = std::fabs(expected - got);
    absErrOut = diff;
    const double denom = std::fabs(expected);
    relErrOut = (denom > 0.0) ? diff / denom : (diff == 0.0 ? 0.0 : INFINITY);
    const int64_t u = ulpsBetween(static_cast<float>(expected), static_cast<float>(got));
    ulpErrOut = (u > INT_MAX) ? INT_MAX : static_cast<int>(u);

    // Exact-match short-circuit: if bit-identical (or both 0 including +/-0),
    // pass regardless of tolerance being set or not.
    if (expected == got) return true;

    bool haveAny = tol.abs || tol.rel || tol.ulp;
    if (!haveAny) return false;

    if (tol.abs && diff <= *tol.abs) return true;
    if (tol.rel && relErrOut <= *tol.rel) return true;
    if (tol.ulp && ulpErrOut <= *tol.ulp) return true;
    return false;
}

void walk(const std::string& path,
          const Value& expected,
          const Value& got,
          const Tolerance& tol,
          const std::string& source,
          std::vector<Diagnostic>& out)
{
    // Tolerance may sharpen at this path.
    std::string locSource = source;
    Tolerance locTol = resolveAt(tol, path, locSource);

    // Shape mismatch: report and stop descending.
    if (expected.kind != got.kind) {
        // Allow int <-> float promotion at a leaf (YAML int vs CTL float).
        auto isNumeric = [](Value::Kind k) {
            return k == Value::Kind::Int  || k == Value::Kind::UInt
                || k == Value::Kind::Float || k == Value::Kind::Half;
        };
        if (!(isNumeric(expected.kind) && isNumeric(got.kind))) {
            Diagnostic d;
            d.path = path;
            d.expectedText = leafText(expected);
            d.gotText = leafText(got);
            d.applied = locTol;
            d.toleranceSource = locSource;
            out.push_back(std::move(d));
            return;
        }
    }

    switch (got.kind) {
      case Value::Kind::Bool: {
          bool eb = (expected.kind == Value::Kind::Bool) ? expected.b : expected.i != 0;
          if (eb != got.b) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
          }
          return;
      }
      case Value::Kind::Int:
      case Value::Kind::UInt: {
          // Integer compare is always exact, whether got.kind is Int or UInt.
          int64_t ev;
          switch (expected.kind) {
            case Value::Kind::Bool:  ev = expected.b ? 1 : 0; break;
            case Value::Kind::Int:   ev = expected.i; break;
            case Value::Kind::UInt:  ev = (int64_t)expected.u; break;
            case Value::Kind::Float: ev = (int64_t)expected.f; break;
            case Value::Kind::Half:  ev = (int64_t)expected.f; break;
            default: ev = 0; break;
          }
          int64_t gv = (got.kind == Value::Kind::UInt) ? (int64_t)got.u : got.i;
          if (ev != gv) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
          }
          return;
      }
      case Value::Kind::Float: {
          double ev = (expected.kind == Value::Kind::Float
                    || expected.kind == Value::Kind::Half)  ? expected.f
                    : (expected.kind == Value::Kind::Int)   ? (double)expected.i
                    : (expected.kind == Value::Kind::UInt)  ? (double)expected.u
                    : (expected.kind == Value::Kind::Bool)  ? (expected.b ? 1.0 : 0.0)
                                                             : 0.0;
          double absErr = 0, relErr = 0;
          int ulpErr = 0;
          if (!compareFloat(ev, got.f, locTol, absErr, relErr, ulpErr)) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.abs_err = absErr;
              d.rel_err = relErr;
              d.ulp_err = ulpErr;
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
          }
          return;
      }
      case Value::Kind::Half: {
          // Expected is typically authored as Float. Round the author's value
          // to nearest-even half before comparing so sub-ULP drift is caught
          // against the actual representable half. Diagnostics render the
          // rounded half bit pattern via Value::Kind::Half describe().
          double raw = (expected.kind == Value::Kind::Float
                     || expected.kind == Value::Kind::Half)  ? expected.f
                     : (expected.kind == Value::Kind::Int)   ? (double)expected.i
                     : (expected.kind == Value::Kind::UInt)  ? (double)expected.u
                     : (expected.kind == Value::Kind::Bool)  ? (expected.b ? 1.0 : 0.0)
                                                              : 0.0;
          half hExp = static_cast<float>(raw);
          const double evHalf = static_cast<double>(static_cast<float>(hExp));
          double absErr = 0, relErr = 0;
          int ulpErr = 0;
          if (!compareFloat(evHalf, got.f, locTol, absErr, relErr, ulpErr)) {
              Diagnostic d;
              d.path = path;
              d.expectedText = Value::makeHalf(raw).describe();
              d.gotText = leafText(got);
              d.abs_err = absErr;
              d.rel_err = relErr;
              d.ulp_err = ulpErr;
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
          }
          return;
      }
      case Value::Kind::String: {
          if (expected.kind != Value::Kind::String || expected.s != got.s) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
          }
          return;
      }
      case Value::Kind::Seq: {
          if (expected.kind != Value::Kind::Seq || expected.seq.size() != got.seq.size()) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
              return;
          }
          for (size_t i = 0; i < got.seq.size(); ++i) {
              std::ostringstream os;
              os << path << "[" << i << "]";
              walk(os.str(), expected.seq[i], got.seq[i], locTol, locSource, out);
          }
          return;
      }
      case Value::Kind::Map: {
          if (expected.kind != Value::Kind::Map) {
              Diagnostic d;
              d.path = path;
              d.expectedText = leafText(expected);
              d.gotText = leafText(got);
              d.applied = locTol;
              d.toleranceSource = locSource;
              out.push_back(std::move(d));
              return;
          }
          for (const auto& kv : got.map) {
              auto it = expected.map.find(kv.first);
              const std::string childPath = path.empty() ? kv.first : path + "." + kv.first;
              if (it == expected.map.end()) {
                  Diagnostic d;
                  d.path = childPath;
                  d.expectedText = "<missing>";
                  d.gotText = kv.second.describe();
                  d.applied = locTol;
                  d.toleranceSource = locSource;
                  out.push_back(std::move(d));
                  continue;
              }
              walk(childPath, it->second, kv.second, locTol, locSource, out);
          }
          // Report expected-only keys.
          for (const auto& kv : expected.map) {
              if (got.map.find(kv.first) == got.map.end()) {
                  const std::string childPath = path.empty() ? kv.first : path + "." + kv.first;
                  Diagnostic d;
                  d.path = childPath;
                  d.expectedText = kv.second.describe();
                  d.gotText = "<missing>";
                  d.applied = locTol;
                  d.toleranceSource = locSource;
                  out.push_back(std::move(d));
              }
          }
          return;
      }
    }
}

} // namespace

void compareTyped(const std::string& basePath,
                  const Value& expected,
                  const Value& got,
                  const Tolerance& baseTolerance,
                  const std::string& baseSource,
                  std::vector<Diagnostic>& out)
{
    walk(basePath, expected, got, baseTolerance, baseSource, out);
}

namespace {

// Walk a dotted/bracketed path against `shape`. Returns pointer to leaf Value,
// or nullptr if the path cannot be resolved (unknown key / out-of-range index /
// intermediate kind mismatch).
const Value* resolvePath(const Value& shape, const std::string& path) {
    const Value* cur = &shape;
    size_t i = 0;
    const size_t N = path.size();
    while (i < N && cur) {
        // Skip leading dot.
        if (path[i] == '.') { ++i; continue; }
        if (path[i] == '[') {
            size_t j = path.find(']', i);
            if (j == std::string::npos) return nullptr;
            const std::string idx = path.substr(i + 1, j - i - 1);
            if (cur->kind != Value::Kind::Seq) return nullptr;
            size_t n = 0;
            try { n = static_cast<size_t>(std::stoul(idx)); } catch (...) { return nullptr; }
            if (n >= cur->seq.size()) return nullptr;
            cur = &cur->seq[n];
            i = j + 1;
            continue;
        }
        // Bare identifier: advance to next '.' or '['.
        size_t j = i;
        while (j < N && path[j] != '.' && path[j] != '[') ++j;
        const std::string key = path.substr(i, j - i);
        if (cur->kind != Value::Kind::Map) return nullptr;
        auto it = cur->map.find(key);
        if (it == cur->map.end()) return nullptr;
        cur = &it->second;
        i = j;
    }
    return cur;
}

} // namespace

void validateTolerancePaths(const std::string& basePath,
                            const Tolerance& tol,
                            const Value& shape,
                            std::vector<Diagnostic>& out)
{
    // The per_field map is the only tolerance form that names a leaf; the
    // top-level abs/rel/ulp apply only where they're meaningful and are
    // harmless on non-FP leaves (short-circuited by exact compare).
    for (const auto& kv : tol.per_field) {
        const std::string& key = kv.first;
        const Tolerance& sub   = kv.second;
        const bool subHasNum   = sub.abs || sub.rel || sub.ulp;

        std::string fullPath;
        if (basePath.empty()) {
            fullPath = key;
        } else if (!key.empty() && key[0] == '[') {
            fullPath = basePath + key;                  // return[2]
        } else {
            fullPath = basePath + "." + key;            // return.r
        }

        const Value* leaf = resolvePath(shape, fullPath);
        if (!leaf) {
            Diagnostic d;
            d.path = fullPath;
            d.expectedText = "existing path in expected shape";
            d.gotText      = "per_field key does not resolve";
            d.toleranceSource = "test.tolerance.per_field";
            out.push_back(std::move(d));
            continue;
        }
        if (subHasNum && leaf->kind != Value::Kind::Float) {
            Diagnostic d;
            d.path = fullPath;
            d.expectedText = "floating-point leaf for abs/rel/ulp tolerance";
            d.gotText      = std::string("non-FP leaf of kind '") + leaf->describe() + "'";
            d.toleranceSource = "test.tolerance.per_field";
            out.push_back(std::move(d));
        }
        // Recurse so nested per_field blocks are validated too.
        validateTolerancePaths(fullPath, sub, *leaf, out);
    }
}

} // namespace ctltest
