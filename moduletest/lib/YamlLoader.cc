#include "YamlLoader.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <sstream>

namespace ctltest {

namespace fs = std::filesystem;

std::string LoadError::format(const std::string& path, int line, int col, const std::string& msg) {
    std::ostringstream os;
    os << path;
    if (line >= 0) os << ":" << line;
    if (col  >= 0) os << ":" << col;
    os << ": " << msg;
    return os.str();
}

namespace {

[[noreturn]] void fail(const std::string& path, const YAML::Node& n, const std::string& msg) {
    const auto mark = n.Mark();
    throw LoadError(path, mark.line + 1, mark.column + 1, msg);
}

[[noreturn]] void failAt(const std::string& path, int line, int col, const std::string& msg) {
    throw LoadError(path, line, col, msg);
}

const YAML::Node require(const YAML::Node& parent, const char* key, const std::string& path) {
    if (!parent[key]) {
        fail(path, parent, std::string("missing required key '") + key + "'");
    }
    return parent[key];
}

Value parseValue(const std::string& path, const YAML::Node& n) {
    switch (n.Type()) {
      case YAML::NodeType::Scalar: {
          // Preserve author's intent: try bool, then int/uint, then double, else string.
          const std::string s = n.Scalar();
          if (n.Tag() == "!" || n.Tag() == "tag:yaml.org,2002:str") {
              return Value::makeString(s);
          }
          // yaml-cpp heuristic: ask it to parse as each type in order.
          try { return Value::makeBool(n.as<bool>()); } catch (...) {}
          // Integers: prefer int64 unless it doesn't fit.
          try {
              int64_t iv = n.as<int64_t>();
              // If the original text looks floating (contains '.' or 'e'/'E'), prefer float.
              if (s.find('.') == std::string::npos
               && s.find('e') == std::string::npos
               && s.find('E') == std::string::npos) {
                  return Value::makeInt(iv);
              }
          } catch (...) {}
          try { return Value::makeFloat(n.as<double>()); } catch (...) {}
          return Value::makeString(s);
      }
      case YAML::NodeType::Sequence: {
          std::vector<Value> seq;
          seq.reserve(n.size());
          for (const auto& elt : n) seq.push_back(parseValue(path, elt));
          return Value::makeSeq(std::move(seq));
      }
      case YAML::NodeType::Map: {
          std::map<std::string, Value> m;
          for (const auto& kv : n) {
              if (!kv.first.IsScalar())
                  fail(path, kv.first, "map keys must be strings");
              m.emplace(kv.first.Scalar(), parseValue(path, kv.second));
          }
          return Value::makeMap(std::move(m));
      }
      case YAML::NodeType::Null:
      case YAML::NodeType::Undefined:
      default:
          fail(path, n, "null/undefined values are not allowed here");
    }
}

Tolerance parseTolerance(const std::string& path, const YAML::Node& n) {
    Tolerance t;
    if (!n.IsMap()) fail(path, n, "tolerance block must be a map");
    for (const auto& kv : n) {
        const std::string k = kv.first.Scalar();
        if (k == "abs")          t.abs = kv.second.as<double>();
        else if (k == "rel")     t.rel = kv.second.as<double>();
        else if (k == "ulp")     t.ulp = kv.second.as<int>();
        else if (k == "per_field") {
            if (!kv.second.IsMap()) fail(path, kv.second, "per_field must be a map");
            for (const auto& ff : kv.second) {
                t.per_field.emplace(ff.first.Scalar(), parseTolerance(path, ff.second));
            }
        }
        else if (k == "per_channel") {
            if (!kv.second.IsMap()) fail(path, kv.second, "per_channel must be a map");
            for (const auto& ff : kv.second) {
                t.per_channel.emplace(ff.first.Scalar(), parseTolerance(path, ff.second));
            }
        } else {
            fail(path, kv.first, std::string("unknown tolerance key '") + k + "'");
        }
    }
    return t;
}

OracleSpec parseOracle(const std::string& yamlPath, const YAML::Node& n) {
    if (!n.IsMap()) fail(yamlPath, n, "oracle must be a map with exactly one of {inline, csv}");
    OracleSpec o;
    int formCount = 0;
    if (n["inline"]) {
        ++formCount;
        o.kind = OracleSpec::Kind::Inline;
        const YAML::Node inl = n["inline"];
        if (!inl.IsMap()) fail(yamlPath, inl, "oracle.inline must be a map<output_name, value>");
        for (const auto& kv : inl) {
            o.inlineExpected.emplace(kv.first.Scalar(), parseValue(yamlPath, kv.second));
        }
    }
    if (n["csv"]) {
        ++formCount;
        o.kind = OracleSpec::Kind::Csv;
        fs::path p = n["csv"].as<std::string>();
        if (p.is_absolute()) {
            o.csvPath = p.string();
        } else {
            const fs::path base = fs::path(yamlPath).parent_path();
            o.csvPath = (base / p).lexically_normal().string();
        }
    }
    if (n["exr"]) {
        ++formCount;
        o.kind = OracleSpec::Kind::Exr;
        fs::path p = n["exr"].as<std::string>();
        if (p.is_absolute()) {
            o.exrRefPath = p.string();
        } else {
            const fs::path base = fs::path(yamlPath).parent_path();
            o.exrRefPath = (base / p).lexically_normal().string();
        }
        if (n["max_failing_pixels"]) {
            o.max_failing_pixels = n["max_failing_pixels"].as<int>();
        }
    }
    if (n["snapshot"]) {
        ++formCount;
        o.kind = OracleSpec::Kind::Snapshot;
        const YAML::Node sn = n["snapshot"];
        std::string pathStr;
        if (sn.IsScalar()) {
            pathStr = sn.as<std::string>();
        } else if (sn.IsMap()) {
            if (!sn["path"]) fail(yamlPath, sn, "oracle.snapshot map requires 'path'");
            pathStr = sn["path"].as<std::string>();
            if (sn["writable"]) o.snapshotWritable = sn["writable"].as<bool>();
        } else {
            fail(yamlPath, sn, "oracle.snapshot must be a string or map<path, writable>");
        }
        fs::path p = pathStr;
        if (p.is_absolute()) {
            o.snapshotPath = p.string();
        } else {
            const fs::path base = fs::path(yamlPath).parent_path();
            o.snapshotPath = (base / p).lexically_normal().string();
        }
    }
    if (formCount != 1) {
        fail(yamlPath, n, "oracle requires exactly one form; v0.5 supports: inline, csv, exr, snapshot");
    }
    return o;
}

ImageSpec parseImage(const std::string& yamlPath, const YAML::Node& n) {
    if (!n.IsMap()) fail(yamlPath, n, "image must be a map with keys {input, input_channels?, output_channels?}");
    ImageSpec s;
    const YAML::Node inp = require(n, "input", yamlPath);
    fs::path p = inp.as<std::string>();
    if (p.is_absolute()) {
        s.inputExrPath = p.string();
    } else {
        const fs::path base = fs::path(yamlPath).parent_path();
        s.inputExrPath = (base / p).lexically_normal().string();
    }
    if (n["input_channels"]) {
        if (!n["input_channels"].IsMap())
            fail(yamlPath, n["input_channels"], "image.input_channels must be a map<ctl_input_arg, exr_channel>");
        for (const auto& kv : n["input_channels"]) {
            s.inputChannelMap.emplace(kv.first.Scalar(), kv.second.as<std::string>());
        }
    }
    if (n["output_channels"]) {
        if (!n["output_channels"].IsMap())
            fail(yamlPath, n["output_channels"], "image.output_channels must be a map<exr_channel, ctl_output_arg>");
        for (const auto& kv : n["output_channels"]) {
            s.outputChannelMap.emplace(kv.first.Scalar(), kv.second.as<std::string>());
        }
    }
    if (n["ulp_precision"]) {
        const std::string u = n["ulp_precision"].as<std::string>();
        if (u != "float" && u != "half" && u != "native") {
            fail(yamlPath, n["ulp_precision"],
                 "image.ulp_precision must be one of: float, half, native (got '" + u + "')");
        }
        if (u == "half") {
            fail(yamlPath, n["ulp_precision"],
                 "image.ulp_precision: half is not yet supported in v1.0 "
                 "(use 'float' and convert tolerances accordingly)");
        }
        s.ulpPrecision = u;
    }
    return s;
}

SweepSpec parseSweep(const std::string& yamlPath, const YAML::Node& n) {
    if (!n.IsMap()) fail(yamlPath, n, "sweep must be a map with keys {inputs, strict?}");
    SweepSpec s;
    const YAML::Node inp = require(n, "inputs", yamlPath);
    fs::path p = inp.as<std::string>();
    if (p.is_absolute()) {
        s.inputsCsvPath = p.string();
    } else {
        const fs::path base = fs::path(yamlPath).parent_path();
        s.inputsCsvPath = (base / p).lexically_normal().string();
    }
    if (n["strict"]) s.strict = n["strict"].as<bool>();
    return s;
}

} // namespace

Suite loadSuite(const std::string& yamlPath) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yamlPath);
    } catch (const YAML::Exception& e) {
        failAt(yamlPath, e.mark.line + 1, e.mark.column + 1, e.msg);
    }
    if (!root.IsMap()) {
        failAt(yamlPath, 1, 1, "top-level document must be a map");
    }

    Suite suite;
    suite.sourcePath = fs::absolute(yamlPath).string();

    if (!root["version"] || root["version"].as<int>() != 1) {
        fail(yamlPath, root, "missing or unsupported 'version' (must be 1)");
    }
    suite.version = 1;

    if (root["suite"]) suite.name = root["suite"].as<std::string>();

    // Modules -- required.
    const YAML::Node mods = require(root, "modules", yamlPath);
    if (!mods.IsSequence() || mods.size() == 0) {
        fail(yamlPath, mods, "'modules' must be a non-empty sequence of strings");
    }
    for (const auto& m : mods) suite.modules.push_back(m.as<std::string>());

    // Module paths -- optional; resolved vs suite file dir.
    if (root["module_paths"]) {
        const fs::path base = fs::path(suite.sourcePath).parent_path();
        for (const auto& p : root["module_paths"]) {
            fs::path rp = p.as<std::string>();
            if (rp.is_absolute()) {
                suite.modulePaths.push_back(rp.string());
            } else {
                suite.modulePaths.push_back((base / rp).lexically_normal().string());
            }
        }
    }

    // Suite-wide defaults.
    if (root["defaults"]) {
        const YAML::Node def = root["defaults"];
        if (def["tolerance"]) suite.defaultTolerance = parseTolerance(yamlPath, def["tolerance"]);
    }

    // Tests.
    const YAML::Node tests = require(root, "tests", yamlPath);
    if (!tests.IsSequence() || tests.size() == 0) {
        fail(yamlPath, tests, "'tests' must be a non-empty sequence");
    }

    for (const auto& tn : tests) {
        if (!tn.IsMap()) fail(yamlPath, tn, "each test must be a map");
        TestCase tc;
        tc.suite = suite.name;
        tc.mode  = TestCase::Mode::Unit;
        tc.modulePaths  = suite.modulePaths;
        tc.extraModules = suite.modules;

        tc.id = require(tn, "id", yamlPath).as<std::string>();
        if (tn["description"]) tc.description = tn["description"].as<std::string>();

        if (tn["mode"]) {
            const std::string mode = tn["mode"].as<std::string>();
            if      (mode == "unit")       tc.mode = TestCase::Mode::Unit;
            else if (mode == "sweep")      tc.mode = TestCase::Mode::Sweep;
            else if (mode == "image")      tc.mode = TestCase::Mode::Image;
            else if (mode == "ctl_native") tc.mode = TestCase::Mode::CtlNative;
            else fail(yamlPath, tn["mode"], "unknown mode '" + mode + "' (expected: unit, sweep, image, ctl_native)");
        }

        const std::string fn = require(tn, "function", yamlPath).as<std::string>();
        tc.functionName = fn;
        tc.moduleName = suite.modules.front();

        if (tc.mode == TestCase::Mode::Sweep) {
            tc.sweep = parseSweep(yamlPath, require(tn, "sweep", yamlPath));
            if (tn["inputs"]) {
                fail(yamlPath, tn["inputs"],
                     "'inputs' is not allowed in sweep mode; inputs come from sweep.inputs CSV");
            }
        } else if (tc.mode == TestCase::Mode::Image) {
            tc.image = parseImage(yamlPath, require(tn, "image", yamlPath));
            if (tn["inputs"]) {
                fail(yamlPath, tn["inputs"],
                     "'inputs' is not allowed in image mode; inputs come from image.input EXR");
            }
        } else if (tc.mode == TestCase::Mode::CtlNative) {
            if (tn["inputs"]) {
                fail(yamlPath, tn["inputs"],
                     "'inputs' is not allowed in ctl_native mode; the CTL function must take no arguments");
            }
            if (tn["oracle"]) {
                fail(yamlPath, tn["oracle"],
                     "'oracle' is not allowed in ctl_native mode; assertions come from testkit::* inside the CTL body");
            }
        } else if (tn["inputs"]) {
            const YAML::Node inps = tn["inputs"];
            if (!inps.IsMap()) fail(yamlPath, inps, "'inputs' must be a map");
            for (const auto& kv : inps) {
                tc.namedInputs.emplace(kv.first.Scalar(), parseValue(yamlPath, kv.second));
            }
        }

        if (tn["returns_name"]) tc.returnsName = tn["returns_name"].as<std::string>();

        tc.tolerance = suite.defaultTolerance;
        if (tn["tolerance"]) {
            tc.tolerance = Tolerance::merge(tc.tolerance, parseTolerance(yamlPath, tn["tolerance"]));
        }

        if (tc.mode != TestCase::Mode::CtlNative) {
            tc.oracle = parseOracle(yamlPath, require(tn, "oracle", yamlPath));
        }

        // In image mode, authors writing `ulp:` must opt into a precision
        // (see ImageSpec::ulpPrecision). Without this, `ulp: 1` is ambiguous
        // between half and float -- the plan resolves this at load time.
        if (tc.mode == TestCase::Mode::Image && tc.tolerance.ulp) {
            auto hasUlpInPerField = [](const Tolerance& t) {
                for (const auto& kv : t.per_field) if (kv.second.ulp) return true;
                for (const auto& kv : t.per_channel) if (kv.second.ulp) return true;
                return false;
            };
            const bool ulpSomewhere = tc.tolerance.ulp || hasUlpInPerField(tc.tolerance);
            if (ulpSomewhere && tc.image.ulpPrecision.empty()) {
                fail(yamlPath, tn,
                     "image mode with ulp: tolerance requires image.ulp_precision "
                     "(float | native); half is not yet supported in v1.0");
            }
        }

        if (tn["ignore_outputs"]) {
            for (const auto& x : tn["ignore_outputs"])
                tc.oracle.ignoreOutputs.push_back(x.as<std::string>());
        }
        if (tn["tags"]) {
            for (const auto& x : tn["tags"])
                tc.tags.push_back(x.as<std::string>());
        }
        if (tn["known_failure"]) tc.knownFailure = tn["known_failure"].as<bool>();

        suite.tests.push_back(std::move(tc));
    }

    return suite;
}

} // namespace ctltest
