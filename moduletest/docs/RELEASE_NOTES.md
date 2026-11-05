# ctltest release notes

`ctltest` is a module-level correctness-testing framework for CTL. Authors
write tests in YAML (or CTL for programmatic cases), the framework marshals
inputs and outputs through the CTL type system, compares against an oracle,
and emits structured results for CI.

This document covers the user-visible surface at each release. For build-
time details (CMake options, dependencies, layout) see the top-level
`moduletest/CMakeLists.txt` and `moduletest/examples/README.md`.

---

## v1.1 -- nested aggregates end-to-end

v1.1 is the first release in which ctltest marshals the full CTL type
system at every boundary -- no more flat-type restriction, no more dropping
per-field tolerances at array-of-struct leaves. The release is driven by a
single fix in the interpreter's path parser, plus the library changes that
fall out of it.

### Highlights

- **Nested aggregates fully supported in YAML inputs and oracles.** Inputs
  and expected outputs can now be arbitrarily nested composites: `float[M][N]`,
  `struct Foo { float v[K]; int n; }`, `Point q[K]`, and combinations thereof.
  The v1.0 load-time rejection of non-flat types has been removed.
- **Per-field tolerances address nested leaves correctly.** Dotted/bracketed
  paths in `per_field` (e.g. `return.v[2]`, `q[0].x`) now resolve through
  multi-segment paths into struct members and array cells. In v1.0 the
  interpreter's path parser silently stopped after the first segment, which
  is why the flat-type restriction existed.
- **C++ unit test binary.** A new `ctltest_unit` binary runs twelve test
  sections against ctltest_core's public API -- Value, Tolerance::merge,
  compareTyped, validateTolerancePaths, CsvTable, YamlLoader, and six
  Marshal round-trip suites (scalar, flat array, struct, nested array,
  struct-with-array, array-of-struct). Registered under a new `ctltest-unit`
  ctest label, distinct from the `ctltest` label used by the YAML
  self-tests.
- **Path-parser regression gate in the core interpreter.** A new
  `unittest/IlmCtl/testPathParser.cpp` test in the core `IlmCtlTest` binary
  exercises the fixed parser with matrix, struct-with-array, and
  array-of-struct fixtures. Uses an explicit `CHECK`/`abort()` macro so the
  test is effective in both Debug and Release builds (under `NDEBUG`,
  `assert()` compiles out).
- **Documentation set.** Seven reference docs now ship under `docs/`:
  - [`QUICKSTART.md`](./QUICKSTART.md) -- five-minute first test.
  - [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) -- every key, every mode, every
    resolved path rule.
  - [`CLI.md`](./CLI.md) -- `ctltest` flags, env vars, exit codes,
    interaction with ctest.
  - [`TESTKIT_API.md`](./TESTKIT_API.md) -- CTL-native escape hatch:
    file/function discovery, `testkit::expect_*` surface, runtime
    mechanism.
  - [`CORE_API.md`](./CORE_API.md) -- C++ `ctltest_core` API for
    downstream embedders.
  - [`ARCHITECTURE.md`](./ARCHITECTURE.md) -- contributor-facing
    component map, invariants, and how to add an oracle / reporter /
    value kind.
  - [`TOLERANCE.md`](./TOLERANCE.md) -- merge order, pass-if-any
    semantics, half rounding, ULP precision, image-mode extras.

### Interpreter fix -- `Ctl::Type::childElementV`

v1.0's flat-type restriction was a workaround for an off-by-one in
`lib/IlmCtl/CtlType.cpp` when parsing slash-separated paths. Given
`"i/j"`, the parser extracted the leaf `"j"` but passed `"i/j"` (minus one
character: `"i/"` becomes `"i"` after a stray adjustment) as the head segment,
producing a malformed recursion that drove `CtlExc::_explain` into stack
overflow on certain inputs.

The fix (one character) lets the parser consume every segment in the path.
With it, `TypeStorage::set("m/1/2", ...)` on a `float[M][N]` argument now
reaches the correct leaf, and by extension struct members and nested
composites all work. No behavior change for any caller using single-segment
paths, which was every existing in-tree caller other than ctltest.

### Marshal surface

- `Marshal::join` now uses `/` as the separator so joined paths are valid
  arguments for `TypeStorage::set/get` directly. v1.0 used `.` for human
  readability and paid for it with the flat-type restriction.
- `validateFlatType` is gone. Load-time rejection of nested types was a
  workaround for the parser bug; with the fix in place the check would
  reject valid tests.

### Upgrading from v1.0

**No breaking changes for valid v1.0 tests.** The flat-type restriction was
the only place v1.0 rejected input that v1.1 accepts, and v1.0 would have
emitted a clear load-time error (`"only flat types supported"`) for any
test that now works under v1.1. If your suite passed on v1.0, it passes on
v1.1.

If you had workarounds for the flat-type restriction -- flattening a
`float[3][3]` into `float[9]` and reshaping inside the CTL function, say,
or splitting a struct into N scalar outputs -- you can now author the
natural shape directly:

```yaml
# v1.0 workaround
inputs:
  m_flat: [1, 2, 3, 4, 5, 6, 7, 8, 9]
oracle:
  inline:
    out_flat: [1, 4, 7, 2, 5, 8, 3, 6, 9]

# v1.1 native
inputs:
  m: [[1,2,3], [4,5,6], [7,8,9]]
oracle:
  inline:
    out: [[1,4,7], [2,5,8], [3,6,9]]
```

Per-field tolerances can now address cells inside nested shapes:

```yaml
# v1.0: only flat per_field keys worked reliably
tolerance:
  abs: 1.0e-5
  per_field:
    "return.x": { abs: 5.0e-4 }

# v1.1: any leaf addressable via the compareTyped path syntax
tolerance:
  abs: 1.0e-5
  per_field:
    "return.v[2]": { abs: 5.0e-4 }
    "q[0].x":      { abs: 1.0e-4 }
```

### Verification

Run everything under the `ctltest` and `ctltest-unit` labels:

```sh
cmake -S . -B build -DCTL_BUILD_TESTS=ON -DCTL_BUILD_MODULETEST=ON
cmake --build build -j
ctest --test-dir build -L 'ctltest|ctltest-unit' --output-on-failure
```

Expect six ctest entries to pass (five YAML self-tests + one C++ unit
binary, 12 inner sections). The IlmCtl path-parser test is part of
`IlmCtlTest` which is exercised by `ctest -L ctl` in the interpreter tier.

---

## v1.0 -- load-time hardening

v1.0 was a consolidation release. No new modes were added; instead the
load-time validation surface was tightened so that authoring mistakes
surface as `LoadError`s with YAML line/col rather than as confusing runtime
diagnostics.

### Highlights

- Load-time rejection of tolerances on non-floating-point leaves
  (bool/int/string). These compare exactly; an `abs:`/`rel:`/`ulp:` on such
  a leaf is always a user mistake.
- Load-time rejection of `per_field` keys that don't resolve in the
  expected-outputs shape. Catches typos before the CTL interpreter is even
  constructed.
- Half values: authored expected values are rounded to nearest-even once
  on load; diagnostics render both the authored decimal and the rounded
  16-bit pattern so sub-ULP drift is legible.
- Sweep CSV `strict` default: extra or missing columns fail load (author
  opts into `strict: false` explicitly).
- Known-failure (`known_failure: true`) inverts the verdict; a passing
  known-failure test emits `UNEXPECTED PASS` and fails, preventing silent
  regressions.

### Constraints (lifted in v1.1)

- Inputs and oracles were restricted to "flat" types: scalars, fixed arrays
  of scalars, and flat structs whose members are all scalars. Any nested
  combination was rejected at load time with an explanatory diagnostic.
  This has been lifted in v1.1; see above.

---

## v0.5 -- snapshot oracle and CTL-native escape hatch

### Highlights

- **`SnapshotOracle`** -- approval-style testing with three-gate write
  protection:
  1. `CTL_TEST_UPDATE_SNAPSHOTS=1` in the environment, or
     `--update-snapshots` on the CLI
  2. Per-test opt-in: `oracle: { snapshot: { writable: true } }`
  3. Explicit `force` flag to overwrite an existing snapshot (the default
     is write-new-only)

  First run without `--allow-new-snapshots` records and exits non-zero so
  CI can't silently green on "no snapshot yet". Subsequent runs read the
  snapshot as the expected value and diff normally.

- **CTL-native escape hatch (`testkit`)** -- for tests that need loops,
  random sampling, property checks, or access to private helpers. Authors
  write `test_<name>.ctl` containing zero-arg `void` functions whose names
  start with `test_`. The framework registers `testkit::expect_*`,
  `testkit::fail`, `testkit::skip`, `testkit::tag`,
  `testkit::set_tolerance`, `testkit::description` as SimdCFuncs that
  append structured records to a per-thread `TestRecorder` -- no stdout
  scraping, unlike the prior community attempt.

- CLI walker skips `snapshots/` subdirectories so `ctltest path/to/suite/`
  doesn't recurse into the side files snapshot tests produce.

---

## v0.4 -- CLI and structured output

### Highlights

- **`ctltest` CLI** -- standalone binary installed alongside `ctlrender`.
  Walks directories of YAML suites, filters by glob, selects a reporter at
  flag time. Thin wrapper around `ctltest_core::Runner` -- 100% of its
  execution path is shared with the in-tree `ctltest_run_one` driver.
- **TAP v14 reporter** -- standard streaming format for CI integration.
- **JUnit XML reporter** -- consumed by Jenkins, GitLab, GitHub Actions.
  Each CTL test becomes a `<testcase>`, each diagnostic a `<failure>`
  entry with structured `expected`/`got`/`abs_err`/`rel_err`/`ulp_err`
  fields.
- `--filter`, `--output`, `--reporter` flags. `--update-snapshots[=force]`
  reserved for v0.5.

### Usage

```sh
# Run one suite, human output:
ctltest tests/selftest/scalar_roundtrip.yaml

# CI: run a directory, emit TAP to stdout:
ctltest --reporter tap tests/

# CI: run a directory, emit JUnit XML to a file:
ctltest --reporter junit --output results.xml tests/
```

---

## v0.3 -- image conformance

### Highlights

- **Image mode** -- reference-EXR conformance tests. Source EXR channels
  are bound to CTL input args (via `input_channel_map` or a natural
  `{R,G,B,A}` pass-through), the function runs per-pixel with batching up
  to `SimdInterpreter::maxSamples()` lanes, and the resulting image is
  diffed against the reference EXR by `ExrOracle`.
- **Per-channel tolerance** -- `per_channel: { R: {...}, G: {...} }`.
- **`max_failing_pixels`** -- cap on per-pixel, per-channel failures before
  a case fails. 0 means "any mismatch fails"; negative means "no cap".
- On failure the CLI can write `<snap>.actual.exr` and `<snap>.diff.exr`
  next to the reference so the author can open both in the same viewer.
- `ImageIO` ships a minimal EXR read path copied from `ctlrender` -- the
  full refactor of `ctlrender/exr_file.{cc,hh}` into a shared module is
  deferred.

### ULP-in-image-mode quirk

When a `ulp:` tolerance is set in image mode, the author must specify
`ulp_precision: float` (v1.0 default) explicitly. `half` and `native` are
reserved for a later release. Mixing `ulp:` with half channels without an
explicit `ulp_precision` is a load-time error.

---

## v0.2 -- sweeps

### Highlights

- **Sweep mode** -- one YAML test parameterizes over the rows of a CSV.
  The runner batches rows into `maxSamples()`-lane varying calls, so a
  1000-row sweep turns into a dozen CTL invocations rather than a
  thousand.
- **`CsvTable` / `CsvOracle`** -- the CSV reader supports comma-separated
  fields, double-quoted fields with `""` escapes, CRLF/LF line endings,
  and `#` comment lines. Column shape is strict by default (extra/missing
  columns fail load) with an opt-out `strict: false` that downgrades to
  warning.
- **Expected-outputs CSV** -- sweep tests can pair an `inputs_csv` with an
  `expected_csv`. Column names match CTL output args and the `returnsName`
  alias.

### Varying-dispatch caveats

- CTL parameters are uniform by default. In sweep mode the runner
  promotes every input and output arg to varying via `setVarying(true)`
  before binding. The public `FunctionArg::setVarying()` docs call this
  "undefined" for outputs, but the Simd backend resizes its internal
  register correctly and `CtlSimdFunctionCall` reconciles the flag after
  the call. This is the same pattern used in the interpreter's own test
  binary.

---

## v0.1 -- foundation

Initial release. Enough framework to obsolete the prior community
`ctlrender`-stdout-grep approach for scalar and fixed-array tests.

### Highlights

- **`moduletest/lib/ctltest_core`** static library with CaseModel, Result,
  YamlLoader, CsvTable, Marshal, InterpRunner, Oracle, InlineOracle,
  Reporter, ConsoleReporter, Runner.
- **`ctltest_run_one`** ctest driver binary. Not installed; invoked only
  by ctest entries generated from `moduletest/tests/manifest.txt` via
  `ctltest_add_yaml()`. 100% of its execution path shared with the v0.4+
  CLI.
- **YAML schema v1**: `version:`, `suite:`, `modules:`, `module_paths:`,
  `defaults.tolerance:`, `tests:` array with `id`, `description`,
  `function`, `inputs`, `oracle.inline`, `tolerance`, `returns_name`,
  `ignore_outputs`, `tags`, `known_failure`.
- **Type coverage (v0.1)**: bool, int, uint, half, float, string; fixed
  arrays of scalars; flat structs. Varying-struct-output rejected at load
  time.
- **CMake integration**: `option(CTL_BUILD_MODULETEST ON)` gates the
  subdirectory; yaml-cpp pulled via `find_package(CONFIG QUIET)` with
  `FetchContent` fallback, mirroring the sleef precedent. C++17 scoped to
  `ctltest_core` via `target_compile_features(... PUBLIC cxx_std_17)`;
  the rest of the project stays on C++11.

---

## Known limitations (as of v1.1)

- **Struct partial-default binding** -- `FunctionArg::hasDefaultValue()` is
  per-arg, not per-field. Struct literals in YAML must be complete;
  partial-struct `defaults: inherit` semantics are deferred.
- **Varying struct outputs** -- emit a "not yet supported" load-time error
  in sweep/image mode. Flat-scalar and array-of-scalar outputs work in all
  modes.
- **Tables > 64 elements** -- must come from a sidecar CSV/cube via
  `from_file:`; authoring raw `values:` arrays above this threshold is
  rejected at load time. The diffs would be unreadable anyway.
- **`ulp_precision: half`/`native`** -- reserved but not implemented; use
  `ulp_precision: float` (or omit ULP in image mode).
- **EXR read path** -- ImageIO copies the ~100 LOC read path from
  `ctlrender/exr_file`. Unifying both into one module is deferred to a
  later release that touches `ctlrender`.
- **Python bindings** -- out of scope for v1.x.

## Directory layout reference

```
moduletest/
  CMakeLists.txt       # reads tests/manifest.txt, adds lib/driver/cli/unittest
  lib/                 # ctltest_core static library
  driver/              # ctltest_run_one (ctest entry)
  cli/                 # ctltest (installed alongside ctlrender)
  cmake/               # CtlTestDiscover.cmake with ctltest_add_yaml()
  unittest/            # C++ unit tests for ctltest_core (ctltest-unit label)
  tests/
    manifest.txt       # git-tracked list of YAML paths for ctest
    selftest/          # framework self-tests
    fixtures/          # minimal CTL modules + EXRs for self-tests
  examples/            # copy-paste-editable authoring examples
  docs/
    RELEASE_NOTES.md   # this document
```

## Further reading

- `moduletest/examples/README.md` -- authoring guide, YAML vs CTL
  decision matrix.
- `moduletest/examples/aces_output_transform.yaml` -- realistic unit +
  image mixed-mode example.
- `moduletest/examples/csv_sweep.yaml` -- sweep-mode example.
- `moduletest/examples/snapshot_approval.yaml` -- snapshot-mode example.
- `moduletest/examples/escape_hatch/` -- CTL-native example with
  `test_mymodule.ctl` and the driving `ctl_tests.yaml`.
