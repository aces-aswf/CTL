# ctltest architecture

This document describes how ctltest is built. It's for contributors who
want to fix a bug, add a feature, or port a new oracle / reporter / value
type. If you're trying to *use* the framework, read
[`QUICKSTART.md`](./QUICKSTART.md) first.

---

## Top-level shape

```
         +---------------+             +---------------+
         |  ctltest      |             | ctltest_run_  |
         |   (CLI bin)   |             |   one (ctest) |
         +------+--------+             +------+--------+
                |                             |
                |    both link against...     |
                v                             v
           +----------------------------------+
           |          ctltest_core            |
           |   (static lib; all the logic)    |
           +----------------------------------+
                         |
                         +- IlmCtl, IlmCtlSimd, IlmCtlMath   (CTL)
                         +- Imath, Half, Iex, OpenEXR        (image + FP types)
                         +- yaml-cpp                         (PRIVATE)
```

Everything that isn't argument parsing or stream-plumbing lives in
`ctltest_core`. The two front-ends are deliberately thin so their
behaviors cannot drift from each other.

There's also a third binary -- `ctltest_unit` -- that runs C++ unit tests
against `ctltest_core`'s public API. It is registered under the
`ctltest-unit` ctest label (vs `ctltest` for YAML-driven tests).

## Data flow for one test case

```
   YAML file                                Reporter output
       |                                          ^
       v                                          |
 +--------------+                          +--------------+
 | YamlLoader   |   loadSuite()            |  Reporter    |
 |              | --------------+          |  (Console /  |
 +--------------+               |          |   TAP /      |
                                v          |   JUnit)     |
                         +--------------+  +------+-------+
                         |   Suite      |         ^
                         |   TestCase   |         |
                         +------+-------+         |
                                |                 |
                                v                 |
                         +--------------+         |
                         |  Runner      |---------+ CaseResult
                         +--+---------+-+         |
                            |         |           |
               +------------+         +--------+  |
               v                               v  |
       +--------------+                +--------------+
       | InterpRunner |                |   Oracle     |
       |              |                |  (Inline /   |
       |  +----------+|                |   Csv / Exr/ |
       |  | SimdInt- || outputs        |   Snapshot)  |
       |  | erpreter ||--------------> |              |
       |  +----------+|                |   uses       |
       |        ^     |                |  compareTyped|
       |        |     |                +--------------+
       |   Marshal    |
       |  (TypeStorage|
       |   set/get)   |
       +--------------+
```

The same pipeline runs for every test mode -- only the source of inputs
(YAML `inputs:`, sweep CSV, image EXR, or none for ctl_native) and the
oracle subclass differ.

## Library layout (`lib/`)

| Component       | Files                         | Purpose |
|-----------------|-------------------------------|---------|
| Case model      | `CaseModel.{h,cc}`, `Result.{h,cc}` | Pure-data shapes: `Value`, `Tolerance`, `TestCase`, `Suite`, `Diagnostic`, `CaseResult`. |
| YAML surface    | `YamlLoader.{h,cc}`           | `Suite loadSuite(path)`. All YAML validation happens here. |
| CSV surface     | `CsvTable.{h,cc}`             | Small CSV parser (header + comma + quotes). |
| Snapshot I/O    | `ValueIO.{h,cc}`              | Read/write snapshot YAML files (map of name to Value). |
| Marshal         | `Marshal.{h,cc}`              | Only place that touches CTL FunctionArg layouts. Uses `TypeStorage` exclusively. |
| Interpreter     | `InterpRunner.{h,cc}`, `TestKit.{h,cc}` | Owns one `SimdInterpreter` per test; `TestKit` registers `testkit::*` SimdCFuncs. |
| Oracles         | `Oracle.{h,cc}`, `InlineOracle.{h,cc}`, `CsvOracle.{h,cc}`, `SnapshotOracle.{h,cc}` | `compareTyped` + one subclass per expected-form. |
| Image diff      | `ImageIO.{h,cc}`, `ImageCompare.{h,cc}` | EXR I/O and per-channel diff (image mode). |
| Reporters       | `Reporter.{h,cc}`, `ConsoleReporter.{h,cc}`, `TapReporter.{h,cc}`, `JUnitReporter.{h,cc}` | Streaming `onSuiteBegin` / `onCaseResult` / `onSuiteEnd`. |
| Dispatch        | `Runner.{h,cc}`               | Glue: for each TestCase, construct runner + oracle + assertions, stream result. |

All files are small and single-purpose by design. If one grows past
~300 lines, that's a signal to split rather than extend.

## Where each concern lives

- **"How is CTL type X marshaled?"** see `Marshal.cc`. Every value kind
  calls `TypeStorage::set` / `get` with a dotted path. Adding a new
  Value kind means adding a case to `fromTypeStorage` / `toTypeStorage`
  and a `Value::make<Kind>` factory in `CaseModel`.
- **"Why does tolerance X apply at path Y?"** see `Oracle.cc`. The
  `resolveAt` helper walks dotted paths into `Tolerance::per_field`.
  `compareFloat` implements the OR semantics over abs/rel/ulp.
- **"Where does the snapshot three-gate check live?"** see
  `SnapshotOracle.cc`. The gate function is `envFlag`; the gate combo
  is evaluated at the top of `check()`.
- **"How is the schema validated?"** see `YamlLoader.cc`. Any new YAML
  key should be rejected by default (unknown-key fail) to preserve the
  load-time-is-better-than-runtime invariant.
- **"Where are testkit::* functions registered?"** see `TestKit.cc`.
  Each new `testkit::expect_*` is about 20 LoC: a SimdCFunc, a
  FunctionType declaration, and a CTL wrapper appended to
  `kTestKitSource`.

## Key invariants

Contributors should preserve these. If a change seems to require
breaking one, talk to the maintainer first.

1. **Marshal only through `TypeStorage::set/get`.** Raw
   `(float*)arg->data()` casts exist elsewhere in the CTL tree but are
   wrong for struct/array composition. The ctltest boundary is the
   single place where these can never be written.
2. **One `SimdInterpreter` per test case.** Modules load into a
   process-wide symbol table inside the interpreter; cross-case
   collisions are the reason `ctlrender` also keys its
   `InterpreterCache` by file. `InterpRunner` is non-copyable and
   non-movable to make this lifetime obvious.
3. **Load-time failures beat run-time failures.** Every authoring
   mistake that can be caught from the YAML (unknown key, wrong mode
   combo, malformed tolerance, `ulp:` without `ulp_precision`, etc.)
   must fail in `YamlLoader`, not silently.
4. **Oracles don't own their tolerance -- the runner merges it first.**
   By the time `compareTyped` is called, `baseTolerance` already has
   `suite.default` merged with `tests[i].tolerance`. Per-field
   sharpening happens inside `compareTyped`. This keeps oracle
   subclasses focused on their expected-form, not on merge logic.
5. **`Runner.h::runSuite` is the single dispatch point.** Both
   `ctltest_run_one` and `ctltest` drive it; keep it that way so CLI
   and ctest paths can't diverge.
6. **Pure-data types stay pure data.** `Value`, `Tolerance`, `TestCase`,
   `Diagnostic`, `CaseResult` have no behavior methods beyond
   describe/merge/factory. They're safe to copy, compare, and serialize.

## Adding things

### A new oracle (e.g. `CubeOracle` for .cube LUTs)

1. Add `CubeOracle.{h,cc}` in `lib/`.
2. Inherit from `Oracle`, implement `check(tc, outputs)` returning `OracleVerdict`.
3. Call `compareTyped` on each expected/actual pair; don't roll your
   own tolerance logic.
4. Add `OracleSpec::Kind::Cube` and path field(s) in `CaseModel.h`.
5. Teach `YamlLoader.cc::parseOracle` a `cube:` form. Remember to
   require exactly-one-form.
6. Hook dispatch into `Runner.cc`.
7. Add a self-test YAML under `moduletest/tests/selftest/` and list it
   in `tests/manifest.txt`.
8. Document it in [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) "Oracle block".

### A new reporter (e.g. GitHub Actions annotations)

1. Add `GitHubReporter.{h,cc}` in `lib/`.
2. Inherit from `Reporter`, implement the three hooks.
3. Write to a passed-in `std::ostream&`; don't assume stdout.
4. Extend `cli/main.cc`'s `ReporterKind` enum and CLI flag.
5. Document in [`CLI.md`](./CLI.md) "Options -- `--reporter`".

### A new `testkit::expect_*` (e.g. `expect_near_f3`)

1. In `TestKit.cc`: add a SimdCFunc, build a FunctionType, call
   `declareSimdCFunc`.
2. Append a CTL wrapper to `kTestKitSource` in the same file.
3. Add an assertion kind to `TestAssertion::Kind`.
4. Teach `Runner.cc::renderAssertions` to format it.
5. Document in [`TESTKIT_API.md`](./TESTKIT_API.md).

### A new `Value::Kind`

Only needed if CTL grows a new leaf type ctltest wants to carry. Touch
points: `Value` factories and describe(), Marshal's TypeStorage
dispatch, `compareTyped`'s kind switch in `Oracle.cc`, and YAML
parsing heuristic in `YamlLoader.cc::parseValue`. Non-trivial; file an
issue before starting.

## Build system

### Per-subdirectory breakdown

- `moduletest/CMakeLists.txt` -- yaml-cpp resolution (find_package or
  FetchContent), subdirectory registration, manifest.txt scan, then
  `add_test` calls via `ctltest_add_yaml()`.
- `lib/CMakeLists.txt` -- `ctltest_core` static lib. Publicly bumps
  `cxx_std_17` so downstream binaries inherit C++17 without affecting
  the rest of CTL (which stays on C++11).
- `driver/CMakeLists.txt` -- `ctltest_run_one` internal binary (not
  installed).
- `cli/CMakeLists.txt` -- `ctltest` installed binary.
- `unittest/CMakeLists.txt` -- `ctltest_unit` C++ unit-test binary;
  registers one `ctltest-unit` ctest entry.

### yaml-cpp

`find_package(yaml-cpp CONFIG QUIET)` first; falls back to
FetchContent at tag 0.8.0 if not found and `CTLTEST_FETCH_YAML_CPP=ON`
(default). The fallback sets `CMAKE_POLICY_VERSION_MINIMUM` to
satisfy newer CMake's refusal to configure projects declaring
`cmake_minimum_required(3.0)`.

Target-name probing accepts both `yaml-cpp::yaml-cpp` (modern) and
`yaml-cpp` (older/bundled).

### C++17 scope

Only `ctltest_core` (and transitively its binaries / the unit test)
compiles at C++17. The rest of CTL stays C++11. The bump is local via
`target_compile_features(ctltest_core PUBLIC cxx_std_17)`. Downstream
consumers who link against `ctltest_core` will inherit C++17 by
propagation.

## ctest integration

Tests under `ctltest`:

- Entries in `moduletest/tests/manifest.txt` get expanded into
  `add_test(NAME "ctltest::<rel>" COMMAND ctltest_run_one <SOURCE>/<rel>)`
  with label `ctltest`.
- No `file(GLOB)` is used on purpose: adding a test is a visible line
  in manifest.txt, which flags in PR review and doesn't trigger silent
  reconfigurations.

Test under `ctltest-unit`:

- `unittest/CMakeLists.txt` registers `ctltest_unit` with
  `WORKING_DIRECTORY` set to its build dir (so its CWD-relative module
  paths resolve) and label `ctltest-unit`.

```
ctest --test-dir build -L ctltest                    # YAML-driven cases
ctest --test-dir build -L ctltest-unit               # C++ unit tests
ctest --test-dir build -L 'ctltest|ctltest-unit'     # everything ctltest owns
```

## Directory map

```
moduletest/
  CMakeLists.txt
  lib/                   ctltest_core static library
  driver/                ctltest_run_one (internal)
  cli/                   ctltest (installed)
  unittest/              ctltest_unit (C++ unit tests)
  cmake/
    CtlTestDiscover.cmake  ctltest_add_yaml() helper
  tests/
    manifest.txt         one line per registered suite
    fixtures/            tiny CTL modules used by self-tests
    selftest/            YAML suites exercising the framework
    realworld/           ad-hoc suites against real ACES modules
                         (not in manifest; run manually)
  examples/              worked copy-paste examples for authors
  docs/                  everything under this directory
```

Self-tests under `tests/selftest/` are intentionally small and
fast-running; each exercises one framework feature. Realworld tests
use absolute paths to external checkouts and are therefore not
manifest-tracked -- they're for local validation of framework changes
against production ACES modules.

## Testing the framework itself

Three layers:

1. **Core interpreter regression**: `unittest/IlmCtl/testPathParser.cpp`
   guards the `CtlType::childElementV` fix that v1.1 depends on.
   Registered under the existing `IlmCtlTest` binary, not under
   ctltest.
2. **C++ unit tests**: `moduletest/unittest/ctltest_unit.cc` runs
   twelve test sections against `ctltest_core`'s public API. Covers
   `Value`, `Tolerance::merge`, `compareTyped`,
   `validateTolerancePaths`, `CsvTable`, `YamlLoader`, and six Marshal
   round-trips (scalar, flat array, struct, nested array, struct-
   with-array, array-of-struct).
3. **YAML self-tests**: `tests/selftest/*.yaml` run against
   `tests/fixtures/*.ctl`. Every framework feature has at least one
   case here. These are the authoritative end-to-end tests -- if a
   change passes the C++ unit tests but breaks a self-test, the
   feature's contract has regressed.

## Further reading

- [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) -- authoring-side keys and their
  meanings.
- [`CORE_API.md`](./CORE_API.md) -- C++ types for embedders.
- [`TOLERANCE.md`](./TOLERANCE.md) -- the compare semantics in detail.
- [`RELEASE_NOTES.md`](./RELEASE_NOTES.md) -- what shipped when.
