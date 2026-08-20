# Writing tests with `ctltest`

This directory holds worked examples for authors starting a new test
suite. Each file is designed to be copy-paste-editable -- pick the one
that most resembles your situation, drop it next to your CTL module,
and adapt.

New here? Read [`../docs/QUICKSTART.md`](../docs/QUICKSTART.md) first.
For the full key-by-key YAML schema see
[`../docs/YAML_SCHEMA.md`](../docs/YAML_SCHEMA.md). For tolerance
semantics see [`../docs/TOLERANCE.md`](../docs/TOLERANCE.md). For the
CLI see [`../docs/CLI.md`](../docs/CLI.md).

## Pick a format: YAML or CTL?

`ctltest` supports two authoring surfaces that share the same oracle
and reporter pipeline. Use this flowchart to pick:

| Question | Use YAML | Use CTL (`testkit`) |
|---|---|---|
| I have a known input/output pair | [x] | -- |
| I want to sweep a CSV through my function | [x] | -- |
| I want to diff an EXR against a reference | [x] | -- |
| I need a loop / random sample / property check | -- | [x] |
| I want to assert intermediate values inside the CTL module | -- | [x] |
| I want to access private helpers that aren't exported | -- | [x] |
| I want a snapshot / approval test | [x] | -- |
| I want structured CI output (JUnit / TAP) | works either way (the CLI emits both) |

In practice most projects are 95% YAML with one or two CTL escape-hatch
files for edge cases.

### Why not "CTL tests only"?

The prior community attempt (aces-unittest-test) ran CTL tests through
`ctlrender` and grepped stdout for "FAILED". That approach cannot carry
tolerances beyond vec3, cannot describe tables or structs, produces no
structured output, and is silently fragile (stdout formatting drift).
`ctltest` uses `testkit::expect_*` functions registered as SimdCFuncs,
so assertions flow as structured records into the host and get
reported the same way YAML tests do.

## Examples in this directory

- `aces_output_transform.yaml` -- a realistic unit-plus-image test suite
  modelled on the kind of verification an Output Transform author would
  write. Covers inline scalars, struct-valued gains, per-field tolerance
  on hue bounds, and an EXR image diff with `ulp_precision: float`.

- `csv_sweep.yaml` -- parameterized sweep driven by an external CSV, with
  `strict: true` so column-drift in the oracle fails loud.

- `snapshot_approval.yaml` -- a snapshot/approval workflow for a function
  whose outputs are too numerous to author by hand. Includes the
  three-gate write protection idiom (env + per-test + `force`).

- `escape_hatch/test_rrt.ctl` -- the CTL-side escape hatch, showing how
  to `import "testkit";` and author `void test_*` entry points. Pair
  with `escape_hatch/ctl_tests.yaml` which declares the escape-hatch
  test as a case with `mode: ctl_native`.

## Authoring checklist

Before you commit a new YAML test, walk this list:

1. **Does every non-defaulted CTL output appear in `oracle.inline` (or
   a CSV column, or an EXR channel)?** Missing outputs are a load-time
   error -- add `oracle.ignore_outputs: [...]` if you deliberately don't
   want to check one.
2. **Do your tolerances live on FP leaves?** `abs`/`rel`/`ulp` on bool
   / int / string leaves are a load-time error. Integer and string
   compares are always exact; add a `normalize: {trim, case_insensitive}`
   key if you need loose string compare.
3. **Is your struct expected a full literal?** v1 requires every field
   to appear -- partial literals fail load. Plan for this by extracting
   common struct defaults into a YAML anchor.
4. **Does your image test declare `ulp_precision`?** Required whenever
   image-mode tolerance uses `ulp:`. `float` is the v1 default choice;
   `half` is not yet supported; `native` is reserved.
5. **Is your sweep CSV strict?** The default is strict-fail on column
   mismatch. Only downgrade to `strict: false` if you explicitly want
   stderr warnings for missing columns.

## Running these

```
# In-repo via ctest (discovers manifest.txt):
cmake -S . -B build -DCTL_BUILD_MODULETEST=ON
cmake --build build -j
ctest --test-dir build -L ctltest

# Out-of-tree CLI (installed alongside ctlrender):
ctltest --reporter tap moduletest/examples/
ctltest --reporter junit --output results.xml moduletest/examples/aces_output_transform.yaml
```
