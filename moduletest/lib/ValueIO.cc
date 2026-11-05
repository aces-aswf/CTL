#include "ValueIO.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace ctltest {

namespace {

Value parseValue(const YAML::Node& n) {
    if (n.IsScalar()) {
        const std::string& s = n.Scalar();
        // Prefer bool > int > float > string; mirrors YamlLoader semantics.
        if (s == "true" || s == "false" || s == "True" || s == "False")
            return Value::makeBool(s == "true" || s == "True");
        // try int
        try {
            size_t pos = 0;
            long long iv = std::stoll(s, &pos);
            if (pos == s.size()) return Value::makeInt(iv);
        } catch (...) {}
        // try double
        try {
            size_t pos = 0;
            double dv = std::stod(s, &pos);
            if (pos == s.size()) return Value::makeFloat(dv);
        } catch (...) {}
        return Value::makeString(s);
    }
    if (n.IsSequence()) {
        std::vector<Value> seq;
        seq.reserve(n.size());
        for (const auto& elt : n) seq.push_back(parseValue(elt));
        return Value::makeSeq(std::move(seq));
    }
    if (n.IsMap()) {
        std::map<std::string, Value> m;
        for (const auto& kv : n) {
            m.emplace(kv.first.Scalar(), parseValue(kv.second));
        }
        return Value::makeMap(std::move(m));
    }
    throw ValueIOError("unsupported YAML node kind");
}

void emitValue(YAML::Emitter& e, const Value& v) {
    switch (v.kind) {
      case Value::Kind::Bool:
          e << (v.b ? "true" : "false");
          return;
      case Value::Kind::Int:
          e << v.i;
          return;
      case Value::Kind::UInt:
          e << v.u;
          return;
      case Value::Kind::Float:
      case Value::Kind::Half: {
          // 17 digits round-trips a double per IEEE 754; snapshot format is
          // lossless for Float and Half (round-to-nearest-even is reapplied
          // on the compare side from the authored value).
          std::ostringstream os;
          os.precision(17);
          os << v.f;
          e << YAML::DoubleQuoted << os.str();
          return;
      }
      case Value::Kind::String:
          e << v.s;
          return;
      case Value::Kind::Seq:
          e << YAML::Flow << YAML::BeginSeq;
          for (const auto& x : v.seq) emitValue(e, x);
          e << YAML::EndSeq;
          return;
      case Value::Kind::Map:
          e << YAML::BeginMap;
          for (const auto& kv : v.map) {
              e << YAML::Key << kv.first << YAML::Value;
              emitValue(e, kv.second);
          }
          e << YAML::EndMap;
          return;
    }
}

} // namespace

std::map<std::string, Value> loadValueMap(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& e) {
        throw ValueIOError(std::string("failed to load '") + path + "': " + e.what());
    }
    if (!root || root.IsNull()) return {};
    if (!root.IsMap())
        throw ValueIOError("snapshot '" + path + "' must be a map<output_name, value>");
    std::map<std::string, Value> out;
    for (const auto& kv : root) {
        out.emplace(kv.first.Scalar(), parseValue(kv.second));
    }
    return out;
}

void saveValueMap(const std::string& path,
                  const std::map<std::string, Value>& values)
{
    YAML::Emitter e;
    e << YAML::BeginMap;
    for (const auto& kv : values) {
        e << YAML::Key << kv.first << YAML::Value;
        emitValue(e, kv.second);
    }
    e << YAML::EndMap;

    const fs::path target = path;
    const fs::path parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }
    const fs::path tmp = target.string() + ".tmp";

    {
        std::ofstream os(tmp.string());
        if (!os) throw ValueIOError("cannot open snapshot tmp file '" + tmp.string() + "'");
        os << "# ctltest snapshot -- edit with care; regenerate with CTL_TEST_UPDATE_SNAPSHOTS=1\n";
        os << e.c_str() << "\n";
        if (!os.good()) {
            os.close();
            std::remove(tmp.string().c_str());
            throw ValueIOError("write failed on snapshot tmp file '" + tmp.string() + "'");
        }
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        std::remove(tmp.string().c_str());
        throw ValueIOError("rename(" + tmp.string() + " to " + target.string() + ") failed: " + ec.message());
    }
}

} // namespace ctltest
