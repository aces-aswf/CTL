#include "CaseModel.h"

#include <half.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace ctltest {

Value Value::makeBool(bool x)              { Value v; v.kind = Kind::Bool;   v.b = x; return v; }
Value Value::makeInt(int64_t x)            { Value v; v.kind = Kind::Int;    v.i = x; return v; }
Value Value::makeUInt(uint64_t x)          { Value v; v.kind = Kind::UInt;   v.u = x; return v; }
Value Value::makeFloat(double x)           { Value v; v.kind = Kind::Float;  v.f = x; return v; }
Value Value::makeHalf(double x)            { Value v; v.kind = Kind::Half;   v.f = x; return v; }
Value Value::makeString(std::string x)     { Value v; v.kind = Kind::String; v.s = std::move(x); return v; }
Value Value::makeSeq(std::vector<Value> x) { Value v; v.kind = Kind::Seq;    v.seq = std::move(x); return v; }
Value Value::makeMap(std::map<std::string, Value> x) {
    Value v; v.kind = Kind::Map; v.map = std::move(x); return v;
}

std::string Value::describe() const {
    std::ostringstream os;
    switch (kind) {
      case Kind::Bool:   os << (b ? "true" : "false"); break;
      case Kind::Int:    os << i; break;
      case Kind::UInt:   os << u; break;
      case Kind::Float:  os << f; break;
      case Kind::Half: {
          // Show decimal + the 16-bit pattern after round-to-nearest-even.
          half h = static_cast<float>(f);
          uint16_t bits;
          std::memcpy(&bits, &h, sizeof(bits));
          os << static_cast<double>(static_cast<float>(h))
             << " (half 0x" << std::hex << std::setw(4) << std::setfill('0') << bits << ")";
          break;
      }
      case Kind::String: os << '"' << s << '"'; break;
      case Kind::Seq: {
          os << '[';
          for (size_t i = 0; i < seq.size(); ++i) {
              if (i) os << ", ";
              os << seq[i].describe();
          }
          os << ']';
          break;
      }
      case Kind::Map: {
          os << '{';
          bool first = true;
          for (const auto& kv : map) {
              if (!first) os << ", ";
              first = false;
              os << kv.first << ": " << kv.second.describe();
          }
          os << '}';
          break;
      }
    }
    return os.str();
}

Tolerance Tolerance::merge(const Tolerance& base, const Tolerance& override_) {
    Tolerance out = base;
    if (override_.abs) out.abs = override_.abs;
    if (override_.rel) out.rel = override_.rel;
    if (override_.ulp) out.ulp = override_.ulp;
    for (const auto& kv : override_.per_field) {
        out.per_field[kv.first] = merge(out.per_field[kv.first], kv.second);
    }
    return out;
}

} // namespace ctltest
