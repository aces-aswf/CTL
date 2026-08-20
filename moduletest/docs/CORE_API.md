# `ctltest_core` C++ API reference

`ctltest_core` is the static library that powers both the in-tree
`ctltest_run_one` driver (ctest's entry point) and the installed
`ctltest` CLI. Downstream projects that want to embed the runner -- a
custom front-end, an IDE integration, an in-process CI harness -- link
against this library.

This document is a reference, not a tutorial. For an overview of how
the pieces fit together see [`ARCHITECTURE.md`](./ARCHITECTURE.md). For
user-facing behavior see [`YAML_SCHEMA.md`](./YAML_SCHEMA.md),
[`CLI.md`](./CLI.md), and [`TOLERANCE.md`](./TOLERANCE.md).

All public symbols live in namespace `ctltest`.

---

## Linking

```cmake
target_link_libraries(your_target PRIVATE ctltest_core)
```

`ctltest_core` propagates `IlmCtl`, `IlmCtlSimd`, `IlmCtlMath`, and (via
the OpenEXR targets when present) Imath / Half / Iex / IlmImf as
PUBLIC dependencies, so consumers don't need to restate them. The
target also sets `cxx_std_17` publicly -- the rest of CTL is C++11, but
the `moduletest/` sub-tree scopes C++17 to itself. yaml-cpp is a
PRIVATE dependency and does not leak.

## Header map

| Header                                               | Main types |
|------------------------------------------------------|------------|
| [`CaseModel.h`](../lib/CaseModel.h)                  | `Value`, `Tolerance`, `OracleSpec`, `SweepSpec`, `ImageSpec`, `TestCase`, `Suite` |
| [`Result.h`](../lib/Result.h)                        | `Diagnostic`, `OracleVerdict`, `CaseResult` |
| [`YamlLoader.h`](../lib/YamlLoader.h)                | `loadSuite`, `LoadError` |
| [`CsvTable.h`](../lib/CsvTable.h)                    | `CsvTable`, `readCsv`, `cellToValue`, `tableAsRows`, `CsvError` |
| [`ValueIO.h`](../lib/ValueIO.h)                      | `loadValueMap`, `saveValueMap`, `ValueIOError` (snapshot format) |
| [`Marshal.h`](../lib/Marshal.h)                      | `toArg`, `fromArg`, `bindNamedInput`, varying/lane variants, `MarshalError` |
| [`InterpRunner.h`](../lib/InterpRunner.h)            | `InterpRunner`, `RunError` |
| [`Oracle.h`](../lib/Oracle.h)                        | abstract `Oracle`, `compareTyped`, `validateTolerancePaths` |
| [`InlineOracle.h`](../lib/InlineOracle.h)            | `InlineOracle` |
| [`CsvOracle.h`](../lib/CsvOracle.h)                  | `CsvOracle` |
| [`SnapshotOracle.h`](../lib/SnapshotOracle.h)        | `SnapshotOracle` |
| [`ImageIO.h`](../lib/ImageIO.h)                      | `Image`, `readExr`, `writeExr` |
| [`ImageCompare.h`](../lib/ImageCompare.h)            | `compareImages`, `writeFailureArtifacts`, `ImageCompareResult` |
| [`Reporter.h`](../lib/Reporter.h)                    | abstract `Reporter` |
| [`ConsoleReporter.h`](../lib/ConsoleReporter.h)      | `ConsoleReporter` |
| [`TapReporter.h`](../lib/TapReporter.h)              | `TapReporter` |
| [`JUnitReporter.h`](../lib/JUnitReporter.h)          | `JUnitReporter` |
| [`Runner.h`](../lib/Runner.h)                        | `runSuite`, `RunCounts` |
| [`TestKit.h`](../lib/TestKit.h)                      | `newTestInterpreter`, `TestAssertion`, `drainAssertions`, `clearAssertions` |

## Domain model -- `CaseModel.h`

### `Value`

A tagged union over the scalar and aggregate types ctltest marshals.

```cpp
struct Value {
    enum class Kind { Bool, Int, UInt, Float, Half, String, Seq, Map };
    Kind kind;
    // one of:
    bool b;  int64_t i;  uint64_t u;  double f;  std::string s;
    std::vector<Value> seq;
    std::map<std::string, Value> map;

    static Value makeBool(bool);
    static Value makeInt(int64_t);
    static Value makeUInt(uint64_t);
    static Value makeFloat(double);
    static Value makeHalf(double);      // payload in .f (double width)
    static Value makeString(std::string);
    static Value makeSeq(std::vector<Value>);
    static Value makeMap(std::map<std::string, Value>);

    std::string describe() const;       // short form for diagnostics
};
```

- `Half` is a distinct kind from `Float` so the oracle rounds to
  nearest-even half before compare and renders the half bit pattern in
  diagnostics (see [`TOLERANCE.md`](./TOLERANCE.md) "Half precision").
- In numeric paths `Half` is treated like `Float` -- any compare that
  accepts `Float` accepts `Half`.

### `Tolerance`

```cpp
struct Tolerance {
    std::optional<double> abs;
    std::optional<double> rel;
    std::optional<int>    ulp;
    std::map<std::string, Tolerance> per_field;
    std::map<std::string, Tolerance> per_channel;

    static Tolerance merge(const Tolerance& base, const Tolerance& override_);
    bool empty() const;
};
```

`merge` returns a new `Tolerance`: an override field replaces the base
field only if the override has it set. See
[`TOLERANCE.md`](./TOLERANCE.md) "Merge order".

### `OracleSpec`

```cpp
struct OracleSpec {
    enum class Kind { Inline, Csv, Exr, Snapshot };
    Kind kind = Kind::Inline;

    // Inline
    std::map<std::string, Value> inlineExpected;

    // Csv
    std::string csvPath;

    // Exr
    std::string exrRefPath;
    int max_failing_pixels = 0;   // 0 = any fails, -1 = no cap, N>0 = tolerate up to N

    // Snapshot
    std::string snapshotPath;
    bool        snapshotWritable = false;

    std::vector<std::string> ignoreOutputs;
};
```

Paths in an `OracleSpec` are resolved to absolute by `YamlLoader`
relative to the suite YAML's directory.

### `TestCase` / `Suite`

See [`CaseModel.h`](../lib/CaseModel.h) for the full definitions --
fields line up one-to-one with YAML keys documented in
[`YAML_SCHEMA.md`](./YAML_SCHEMA.md).

## Parse -- `YamlLoader.h`

```cpp
Suite loadSuite(const std::string& yamlPath);
```

Loads and validates a suite YAML. Throws `LoadError` with
`path:line:col: message` on any malformed input, missing required key,
unknown tolerance key, or invalid mode.

`LoadError` exposes `path()`, `line()`, `col()` for programmatic
formatting.

Validation performed at load time:

- `version: 1` gate.
- Required keys per test (`id`, `function`, mode-appropriate
  `inputs` / `sweep` / `image` / `oracle`).
- Oracle disjointness (exactly one of `inline`, `csv`, `exr`,
  `snapshot`).
- `ulp_precision` presence if image tolerance uses `ulp:`.
- Per-mode forbidden-key checks (`inputs:` not allowed in `sweep` /
  `image` / `ctl_native`; `oracle:` not allowed in `ctl_native`).

Validation *not* performed at load time (deferred to run time because
it requires the interpreter): tolerance-path resolution against the
actual CTL signature. That's done by
`Oracle.h::validateTolerancePaths` once outputs are known.

## Interpreter -- `InterpRunner.h`

```cpp
class InterpRunner {
public:
    InterpRunner();
    ~InterpRunner();

    // Neither copyable nor movable (unique_ptr<Impl> + user-declared dtor).
    InterpRunner(const InterpRunner&) = delete;
    InterpRunner& operator=(const InterpRunner&) = delete;

    void setModulePaths(const std::vector<std::string>& paths);
    void loadModules(const std::vector<std::string>& names);

    std::map<std::string, Value> run(
        const std::string& functionName,
        const std::map<std::string, Value>& inputs,
        const std::string& returnsName);

    std::vector<std::map<std::string, Value>> runBatch(
        const std::string& functionName,
        const std::vector<std::map<std::string, Value>>& rows,
        const std::string& returnsName);

    FunctionSignature signature(const std::string& functionName);

    std::vector<TestAssertion> runCtlNative(const std::string& functionName);
};
```

**Lifetime rules:**

- One `InterpRunner` per test case. Modules load into a `SimdInterpreter`
  that holds a process-wide symbol table; keeping runners per-case
  avoids cross-case symbol collisions. (`ctlrender` does the same via
  its `InterpreterCache`.)
- Not copyable or movable -- hold by reference or `unique_ptr`. If you
  need a factory, write a function that takes a reference and
  configures it in place.

**Exceptions:** `RunError` on load/bind failure; CTL runtime exceptions
(`Iex::*`) propagate out unchanged.

**`setModulePaths` is process-global** via
`Ctl::Interpreter::setModulePaths`. The runner restores the previous
paths on destruction. If you construct two runners concurrently, paths
will interleave -- synchronize externally.

## Marshal -- `Marshal.h`

Thin, deliberately small layer between `Value` and `Ctl::FunctionArg`.
Uses `TypeStorage::set`/`get` (from `CtlTypeStorage.h`) exclusively, so
all CTL type layout knowledge lives in one place.

```cpp
void  toArg(const Value& v, Ctl::FunctionArgPtr arg);
Value fromArg(Ctl::FunctionArgPtr arg);

void  toArgLane  (const Value& v, Ctl::FunctionArgPtr arg, size_t lane);
Value fromArgLane(Ctl::FunctionArgPtr arg, size_t lane);

void bindNamedInput(const std::map<std::string, Value>& inputs,
                    Ctl::FunctionArgPtr arg);

void bindNamedInputLane(const std::map<std::string, Value>& inputs,
                        Ctl::FunctionArgPtr arg, size_t lane);
```

`bindNamedInput` mirrors `ctlrender`'s named-parameter binding: if the
arg's name is in `inputs`, bind that Value; otherwise if the arg has a
default, apply it; otherwise throw `MarshalError`.

**Do not write your own FunctionArg layout code.** Raw `(float*)arg->data()`
casts appear elsewhere in the CTL tree (see `unittest/IlmCtl/testCppCall.cpp`)
but those are interpreter-internal tests. The ctltest boundary is
`TypeStorage` only, and keeping it that way is how the framework stays
correct across struct/array/nested composites.

## Oracles -- `Oracle.h` + subclasses

```cpp
class Oracle {
public:
    virtual ~Oracle() = default;
    virtual OracleVerdict check(
        const TestCase& tc,
        const std::map<std::string, Value>& outputs) const = 0;
};
```

Each concrete oracle (`InlineOracle`, `CsvOracle`, `ExrOracle`,
`SnapshotOracle`) constructs a verdict by walking the expected and
observed trees via `compareTyped`:

```cpp
void compareTyped(const std::string&     basePath,
                  const Value&           expected,
                  const Value&           got,
                  const Tolerance&       baseTolerance,
                  const std::string&     baseSource,
                  std::vector<Diagnostic>& out);
```

- `basePath` is the output-arg name for `unit` oracles; empty for the
  top level of a snapshot map.
- `baseTolerance` should already have `suite.default` merged with
  `tests[i].tolerance` before this call; per-field sharpening happens
  inside `compareTyped`.
- `baseSource` is a human-readable tag describing where the tolerance
  came from; diagnostics carry it through to reporters.

`validateTolerancePaths(basePath, tol, shape, out)` sanity-checks that
every `per_field` key resolves to a leaf of the right kind in `shape`.
Call this once the expected shape is known -- usually right after
building the oracle for a test.

## Reporters -- `Reporter.h` + subclasses

```cpp
class Reporter {
public:
    virtual void onSuiteBegin(const std::string&, size_t) = 0;
    virtual void onCaseResult(const CaseResult&) = 0;
    virtual void onSuiteEnd(size_t p, size_t f, size_t e, size_t s, size_t u) = 0;
};
```

`ConsoleReporter`, `TapReporter`, `JUnitReporter` all write to a
`std::ostream&` passed at construction. `TapReporter::finalizePlan()`
and `JUnitReporter::finalize()` must be called after the last suite to
flush format-specific tail content (plan line, closing XML).

To add a format: implement this interface, link to `ctltest_core`, and
your caller can hand an instance to `runSuite`.

## Runner -- `Runner.h`

```cpp
struct RunCounts {
    size_t passed, failed, errored, skipped, unexpectedPass;
    size_t total() const;
    size_t nonPassing() const;   // failed + errored + unexpectedPass
};

RunCounts runSuite(const Suite& suite, Reporter& reporter);
```

Dispatches every case in the suite, calling the reporter's three hooks
in order. `nonPassing() == 0` is the canonical "did this run pass"
check -- that's what the CLI uses to decide its exit code.

## Snapshot format -- `ValueIO.h`

Snapshot files are YAML maps of output-arg-name to Value. Use
`saveValueMap` / `loadValueMap` if you're writing a non-snapshot tool
that wants to read them. Format:

```yaml
# ctltest snapshot -- edit with care; regenerate with CTL_TEST_UPDATE_SNAPSHOTS=1
aOut: "1"
bOut: "0.16878429055213928"
gOut: "0"
rOut: "0.99727851152420044"
```

Numbers are stringified at full round-trip precision. The "edit with
care" banner is intentional -- the file is meant to be regenerated, not
hand-edited.

## TestKit / ctl_native -- `TestKit.h`

```cpp
Ctl::SimdInterpreter* newTestInterpreter();  // caller owns

struct TestAssertion {
    enum class Kind { ExpectNearF, ExpectTrue, Fail };
    Kind kind;
    bool passed;
    std::string message;      // Fail kind
    double actual, expected, abs_tol, abs_err;
};

std::vector<TestAssertion> drainAssertions();
void                       clearAssertions();
```

Typical flow (the framework does this for you in ctl_native mode):

```cpp
clearAssertions();
fn->callFunction(1);
std::vector<TestAssertion> asserts = drainAssertions();
```

`newTestInterpreter()` returns a `Ctl::SimdInterpreter` subclass with
`testkit::expect_*` SimdCFuncs registered and the testkit CTL module
preloaded. `InterpRunner` uses this factory internally -- you rarely
need to call it from client code.

The assertion buffer is `thread_local`. If you spin up your own
parallelism around `runBatch` or `runCtlNative`, each thread drains its
own buffer.

## Complete example: embed the runner

```cpp
#include "YamlLoader.h"
#include "ConsoleReporter.h"
#include "Runner.h"

int main(int argc, char** argv) {
    ctltest::Suite suite = ctltest::loadSuite(argv[1]);
    ctltest::ConsoleReporter rep(std::cout, /*color=*/true);
    ctltest::RunCounts counts = ctltest::runSuite(suite, rep);
    return counts.nonPassing() == 0 ? 0 : 1;
}
```

Same as `ctltest_run_one`, minus the CLI argument handling.

## Stability promises (v1.1)

- **`Value`, `Tolerance`, `OracleSpec`, `TestCase`, `Suite`**: stable.
  Fields may be added; existing fields won't move or change meaning
  inside v1.
- **`InterpRunner` API**: stable. `runBatch` and `runCtlNative` are the
  only additions since v0.1.
- **`Oracle` and `Reporter` interfaces**: stable. Adding a new concrete
  oracle / reporter type is the non-breaking path.
- **`TestKit`**: expanding -- `expect_near_*` variants planned. Existing
  `TestAssertion::Kind` values won't be renumbered.
- **Snapshot YAML format**: stable inside v1. The banner comment is
  informative; tooling should read the data, not parse the banner.
