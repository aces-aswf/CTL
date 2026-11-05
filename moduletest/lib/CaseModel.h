#ifndef CTLTEST_CASE_MODEL_H
#define CTLTEST_CASE_MODEL_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ctltest {

// Value -- an authored or observed CTL value. Scalar leaves, plus recursive
// composition for arrays (Seq) and structs (Map).
//
// v0.1 supports: bool, int, uint, float (half authored as float then rounded
// on marshal), string, fixed arrays, flat structs, and nested composites.
// Tables, varying lanes, and strings in arithmetic positions are v0.2+.
struct Value {
    // Half is a distinct kind so the oracle can round the author's expected
    // value to nearest-even half on compare and surface the 16-bit pattern in
    // diagnostics. From the type-compat side Half is treated as Float (any
    // compare that handles Float also handles Half).
    enum class Kind { Bool, Int, UInt, Float, Half, String, Seq, Map };

    Kind kind = Kind::Float;
    bool b = false;
    int64_t i = 0;
    uint64_t u = 0;
    double f = 0.0;             // carries Float, Half payloads (double-width)
    std::string s;
    std::vector<Value> seq;
    std::map<std::string, Value> map;

    static Value makeBool(bool x);
    static Value makeInt(int64_t x);
    static Value makeUInt(uint64_t x);
    static Value makeFloat(double x);
    static Value makeHalf(double x);
    static Value makeString(std::string x);
    static Value makeSeq(std::vector<Value> x);
    static Value makeMap(std::map<std::string, Value> x);

    std::string describe() const;   // short human form for diagnostics
};

// Tolerance -- per-path tolerances compose from suite default to test default
// to per_field overrides (keyed by dotted path like "out.r" or "out[2].g").
// per_channel mirrors per_field but keyed by EXR channel name (image mode).
struct Tolerance {
    std::optional<double> abs;
    std::optional<double> rel;
    std::optional<int>    ulp;

    // Dotted-path overrides. Resolution order at check time:
    //   1. exact path match in per_field
    //   2. fall through to {abs, rel, ulp}
    //   3. hard default = exact
    std::map<std::string, Tolerance> per_field;
    std::map<std::string, Tolerance> per_channel;

    static Tolerance merge(const Tolerance& base, const Tolerance& override_);
    bool empty() const {
        return !abs && !rel && !ulp && per_field.empty() && per_channel.empty();
    }
};

// OracleSpec -- v0.5 supports Inline, Csv, Exr, and Snapshot.
struct OracleSpec {
    enum class Kind { Inline, Csv, Exr, Snapshot };

    Kind kind = Kind::Inline;

    // For Inline: expected outputs by name. Use "return" for the CTL return
    // value (alias configurable via TestCase::returnsName).
    std::map<std::string, Value> inlineExpected;

    // For Csv: absolute path to the expected-outputs CSV. Columns are matched
    // by name to output-arg / returnsName entries.
    std::string  csvPath;

    // For Exr: absolute path to the reference EXR. The set of channels in the
    // reference drives comparison; extras on the actual side are flagged.
    std::string  exrRefPath;

    // Image-mode only: cap on per-pixel, per-channel failures before a case
    // is declared failing. 0 means "any mismatch fails". <0 means "no cap".
    int max_failing_pixels = 0;

    // For Snapshot: path to the on-disk YAML snapshot. The snapshot contains
    // one top-level key per CTL output / returnsName, shape-compatible with
    // oracle.inline. See SnapshotOracle for three-gate write protection.
    std::string  snapshotPath;

    // Per-test write opt-in (gate 2 of 3). Defaults to false: the test will
    // never overwrite a snapshot even if the env gate is set.
    bool         snapshotWritable = false;

    // Output names the author explicitly doesn't want checked.
    std::vector<std::string> ignoreOutputs;
};

// SweepSpec -- used when TestCase::mode == Sweep. One CSV supplies the named
// inputs for N rows; the runner batches those N rows into lanes of
// Interpreter::maxSamples() and invokes callFunction(N).
struct SweepSpec {
    std::string inputsCsvPath;   // absolute path to inputs CSV
    bool strict = true;          // unknown/missing columns give a load error; else a warning
};

// ImageSpec -- used when TestCase::mode == Image. A source EXR is read into
// per-channel float32 planes and fed through the CTL function pixel-by-pixel
// (width*height samples, batched into maxSamples() lanes). Output channels
// are collected into an Image and handed to ExrOracle for the diff.
//
// inputChannelMap : CTL input-arg name to source EXR channel name.
// outputChannelMap: destination EXR channel name to CTL output-arg name.
// If either map is empty the runner assumes a natural {R,G,B,A} pass-through
// by matching on shared names.
struct ImageSpec {
    std::string inputExrPath;
    std::map<std::string, std::string> inputChannelMap;
    std::map<std::string, std::string> outputChannelMap;

    // When tolerance carries `ulp:` in image mode, this key disambiguates the
    // unit. "float" (v1.0 default) computes ULP distance on float32. "half" is
    // not yet supported. "native" is reserved for v2 (read pixel type from the
    // EXR and pick per-channel). Empty means the author hasn't set it, in
    // which case any use of `ulp:` in tolerance is a load-time error.
    std::string ulpPrecision;
};

// TestCase -- one concrete thing the runner executes.
struct TestCase {
    enum class Mode { Unit, Sweep, Image, CtlNative };

    Mode        mode = Mode::Unit;
    std::string suite;
    std::string id;
    std::string description;

    // CTL module + function under test.
    std::string                moduleName;   // e.g. "ACESlib.RRT"
    std::string                functionName; // e.g. "RRT"
    std::vector<std::string>   extraModules; // loaded in order before the main one
    std::vector<std::string>   modulePaths;  // prepended to interpreter search paths

    // Inputs keyed by CTL parameter name. Missing writable parameters with a
    // default value fall back to hasDefaultValue/setDefaultValue; missing
    // parameters without a default are a runtime error.
    //
    // Unit mode only. Sweep mode populates inputs from SweepSpec row-by-row.
    std::map<std::string, Value> namedInputs;

    SweepSpec   sweep;           // populated iff mode == Sweep
    ImageSpec   image;           // populated iff mode == Image
    OracleSpec  oracle;
    Tolerance   tolerance;

    // How the return value is addressed in oracle/tolerance paths.
    std::string returnsName = "return";

    // Tags for filtering; known-failure inversion (v0.5).
    std::vector<std::string> tags;
    bool knownFailure = false;
};

// Suite -- a YAML document: a version header plus N test cases sharing
// module_paths and defaults.
struct Suite {
    int         version = 1;
    std::string name;
    std::string sourcePath;     // absolute path of the loaded YAML

    std::vector<std::string> modulePaths;   // relative paths resolved against sourcePath's dir
    std::vector<std::string> modules;       // loaded once per interpreter

    Tolerance defaultTolerance;
    std::vector<TestCase> tests;
};

} // namespace ctltest

#endif
