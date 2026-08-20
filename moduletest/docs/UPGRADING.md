# Upgrading ctltest

Short, per-hop upgrade notes. For the full release description of any
version see `RELEASE_NOTES.md`.

## v1.0 to v1.1

**Breaking changes:** none.

**New capability:** YAML inputs and oracles may now use nested shapes --
`float[M][N]`, `struct { float v[K]; int n; }`, arrays of structs, and any
combination thereof. In v1.0 these were rejected at load time with
`"only flat types supported"`.

**What to update:**

- Any v1.0 workaround that flattened nested shapes into `float[N]` outputs
  can be authored in the CTL-native shape directly. No runtime behavior
  change -- existing flat tests keep passing.
- Per-field tolerances can now address leaves through nested paths:
  `return.v[2]`, `q[0].x`, `return[3].inner.x`. If you had to hoist such
  leaves into flat outputs purely for tolerance purposes, you can inline
  them again.

**What NOT to update:**

- Suite-level `tolerance:` blocks, sweep CSVs, image mode, snapshot mode,
  CTL-native escape hatch, and reporter selection all behave identically.
- CMake options (`CTL_BUILD_MODULETEST`, `CTLTEST_FETCH_YAML_CPP`) are
  unchanged.

**New ctest label:** `ctltest-unit` is registered for the new
`ctltest_unit` C++ test binary. The existing `ctltest` label still targets
the YAML self-tests only, so any CI pipeline filtering on it continues to
work. To run everything: `ctest -L 'ctltest|ctltest-unit'`.

## v0.x to v1.0

v1.0 tightened load-time validation. Authoring patterns accepted silently
in v0.x but meaningless (e.g. `abs:` on a bool leaf, a `per_field` key
that doesn't resolve) now raise `LoadError`. Fix the reported paths; no
mechanical migration.

## v0.4 to v0.5

`oracle: { snapshot: ... }` becomes the preferred form for approval tests.
Use `--update-snapshots` (or `CTL_TEST_UPDATE_SNAPSHOTS=1` env) to record
a baseline, then commit the snapshot file next to the YAML.

## v0.3 to v0.4

The `ctltest` CLI joins `ctltest_run_one`. If a CI pipeline was calling
`ctltest_run_one` directly, it still works -- but new pipelines should
invoke the installed `ctltest` binary instead so `--reporter tap` or
`--reporter junit` can emit structured output.

## v0.2 to v0.3

Image conformance suites (`mode: image`) become available. Authors who
want CTL functions verified against reference EXRs can now do so without
writing a sweep by hand.

## v0.1 to v0.2

Sweep mode (`mode: sweep`) lets one YAML parameterize over a CSV instead
of enumerating N inline `unit` tests. If a suite has many nearly-identical
test blocks, they collapse into one.
