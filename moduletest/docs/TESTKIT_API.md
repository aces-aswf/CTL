# `testkit.ctl` API reference

`testkit` is the CTL-side surface of ctltest's escape hatch. Use it when
YAML can't express what you need -- loops, property checks, assertions on
intermediate values inside a multi-stage computation, access to private
module helpers not exposed in the public API.

See [`QUICKSTART.md`](./QUICKSTART.md) for the YAML side, and
[`examples/escape_hatch/`](../examples/escape_hatch/) for a working
example.

---

## File layout

A CTL-native test lives in a `.ctl` file whose basename starts with
`test_`. Each zero-argument `void` function whose name starts with
`test_` is a separate test case.

```ctl
// tests/test_mymodule.ctl
ctlversion 1;

import "testkit";
import "mymodule";

namespace test_mymodule
{

void
test_identity_is_identity()
{
    testkit::expect_near_f(mymodule::identity_f(0.25), 0.25, 1e-7);
}

} // namespace test_mymodule
```

You also need a companion YAML entry that tells ctltest to dispatch it:

```yaml
# tests/ctl_tests.yaml
version: 1
suite: ctl-native-tests

modules:
  - test_mymodule

module_paths:
  - .

tests:
  - id: identity_is_identity
    mode: ctl_native
    function: test_mymodule::test_identity_is_identity
```

`ctl_native` mode forbids `inputs:` and `oracle:` -- the body is the
oracle.

## Imported symbols

A module that does `import "testkit";` gets the following functions in
the `testkit::` namespace:

### `testkit::expect_near_f(float actual, float expected, float abs_tol)`

Record an assertion that `|actual - expected| <= abs_tol`. Always
records -- a passing assertion is captured so reporters can show what
ran, not just what failed.

```ctl
testkit::expect_near_f(f(0.0), 0.0, 1e-7);
```

### `testkit::expect_true(bool cond)`

Record an assertion that `cond` is true.

```ctl
testkit::expect_true(linear[0] > 0.03);
testkit::expect_true(linear[0] < 0.04);
```

## What counts as a test

- The file basename must start with `test_`.
- The function must live in a CTL module, take zero arguments, and
  return `void`.
- The function name must start with `test_`.
- The function must be invocable at uniform lane count 1 -- CTL-native
  tests run at `callFunction(1)` with no input arguments.

`testkit::*` calls inside helper functions are fine; assertions get
attributed to the enclosing test function that made the CTL call that
ran them (because the assertions ride on the per-thread TestKit buffer
drained after `callFunction(1)` returns).

## What happens at runtime

Each call goes through:

1. The framework instantiates an interpreter via `newTestInterpreter()`.
2. The testkit CTL module source is preloaded inline -- you don't need a
   `module_paths:` entry for testkit itself.
3. The testkit functions dispatch to SimdCFuncs which append structured
   records to a per-thread buffer.
4. After `callFunction(1)` returns, the host drains the buffer. Each
   assertion becomes a `Diagnostic` reported through the same pipeline
   as YAML oracle mismatches.
5. A passing assertion records `passed = true` and contributes to the
   test's "N assertions ran" summary.
6. A failing assertion records `passed = false` and fails the test.
7. An uncaught CTL runtime exception (div-by-zero, OOB index, etc.) is
   caught by the host and becomes a single `Assertion{kind = Fail,
   message = <exception text>}` that fails the test.

Nothing is parsed out of stdout. Earlier community attempts (the
aces-unittest-test project) used stdout greps, which cannot carry
tolerances, structs, or diagnostics. `testkit` is the structured
replacement.

## Not yet supported

v1.1's escape hatch is deliberately minimal. The following are planned
but not shipped:

- Vector / struct assertion variants: `expect_near_f3`, `expect_near_struct`.
  Use a helper that calls `expect_near_f` component-wise for now.
- `fail(string)` / `skip(string)` / `set_tolerance(float)` /
  `description(string)` / `tag(string)`. The plan's full surface --
  work item tracked.
- ULP-based scalar assertions: `expect_near_ulp_f`. Use abs for now and
  check the reporter output to see the observed ULP error.
- Integer / bool equality helpers beyond `expect_true`.

Each missing variant is ~20 lines of glue: a SimdCFunc in
`moduletest/lib/TestKit.cc`, a FunctionType declaration, and a CTL-side
wrapper appended to the embedded testkit module source.

## Pitfalls

- **The file basename matters.** A file named `my_checks.ctl` will not
  be picked up; it must start with `test_`.
- **Function name matters too.** A function named `check_linearity()`
  in a `test_*.ctl` file is not a test -- it's just a helper.
- **No arguments.** A test function that takes arguments will fail
  dispatch. If you need parameterization, use a YAML `sweep` oracle, or
  author a loop in the body with `for`.
- **Tests run at lane 1.** Don't write tests that rely on varying
  behavior; assertions read lane 0 only. Exercise varying dispatch
  through sweep or image mode instead.
- **Thread locality.** The assertion buffer is per-thread. Don't spawn
  CTL threads inside a test -- all reachable code runs on the thread
  that called `callFunction(1)`.

## Further reading

- [`examples/escape_hatch/test_mymodule.ctl`](../examples/escape_hatch/test_mymodule.ctl)
  -- a worked example showing the three idioms (roundtrip, property
  check, intermediate-value assertion).
- [`lib/TestKit.cc`](../lib/TestKit.cc) -- the host-side registrar.
  Contains the embedded testkit CTL source at `kTestKitSource`.
- [`ARCHITECTURE.md`](./ARCHITECTURE.md) -- how the escape hatch slots
  into the framework's overall shape.
