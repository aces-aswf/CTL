# ctltest YAML schema reference

This document defines every key that a ctltest suite YAML may contain.
It describes the shape v1.1 accepts; anything else is a load-time error
reported as `file:line:col: message`.

If you're new to ctltest, read [`QUICKSTART.md`](./QUICKSTART.md) first.

---

## Top-level document

A suite is a single YAML mapping:

| Key            | Required | Type                     | Notes |
|----------------|----------|--------------------------|-------|
| `version`      | yes      | int (must be `1`)        | Schema version gate. |
| `suite`        | no       | string                   | Human-readable suite name; surfaces in reporters. |
| `modules`      | yes      | sequence of string       | CTL modules to load, in order. Must be non-empty. |
| `module_paths` | no       | sequence of string       | Added to the interpreter's module search path. Relative entries resolve against the YAML file's directory. |
| `defaults`     | no       | map                      | Suite-wide defaults; currently only `defaults.tolerance` (see below). |
| `tests`        | yes      | sequence of test entries | Non-empty list of test cases (see "Test entry"). |

Example skeleton:

```yaml
version: 1
suite: my-output-transform
modules:
  - ACESlib.Utilities
  - ACES_OT_Rec709
module_paths:
  - ../aces-dev/transforms/ctl
defaults:
  tolerance:
    abs: 1.0e-5
tests:
  - id: ...
    ...
```

## Test entry

Each element of `tests:` is a mapping with these keys:

| Key              | Required | Type                  | Notes |
|------------------|----------|-----------------------|-------|
| `id`             | yes      | string                | Unique within the suite. Used by `--filter` and in reporter output. |
| `description`    | no       | string                | Free-form, multi-line OK. |
| `mode`           | no       | enum                  | `unit` (default), `sweep`, `image`, `ctl_native`. |
| `function`       | yes      | string                | `module::name` or bare `name`. The first of `modules:` is used if bare. |
| `returns_name`   | no       | string (default `return`) | Name used to address the return value in oracles and tolerance paths. |
| `inputs`         | *        | map<string, Value>    | CTL parameter name to Value. Required in `unit` mode; **forbidden** in `sweep` / `image` / `ctl_native`. |
| `sweep`          | *        | map                   | Required iff `mode: sweep`. See "Sweep block". |
| `image`          | *        | map                   | Required iff `mode: image`. See "Image block". |
| `tolerance`      | no       | map                   | Merged over `defaults.tolerance`. See "Tolerance block". |
| `oracle`         | **        | map                   | Required iff `mode != ctl_native`. Must contain exactly one of `inline`, `csv`, `exr`, `snapshot`. |
| `ignore_outputs` | no       | sequence of string    | CTL output-arg names deliberately not checked. |
| `tags`           | no       | sequence of string    | Free-form labels; surface in reporters. |
| `known_failure`  | no       | bool (default false)  | If true, a failing outcome becomes pass. A passing outcome becomes `UnexpectedPass` (a loud failure) so silent regressions are caught. |

* Exactly the keys the mode demands, and no others.
** Forbidden in `ctl_native` mode -- assertions flow from `testkit::*` inside the CTL body instead.

## Values & types

YAML scalars, sequences, and maps map into CTL types as follows:

| YAML form                       | CTL types it binds to                          |
|---------------------------------|------------------------------------------------|
| `true` / `false`                | `bool`                                         |
| `42`, `-1`, `0xff`              | `int`, `unsigned int`, `half`, `float`         |
| `3.14`, `1.0e-6`                | `half`, `float` (half rounds to nearest-even at load; see [`TOLERANCE.md`](./TOLERANCE.md)) |
| `"hi"`                          | `string`                                       |
| `[1, 2, 3]`                     | fixed array: `float[3]` / `int[3]` / `half[3]` |
| `[[1,2,3],[4,5,6]]`             | nested fixed array: `float[2][3]`, etc.        |
| `{ x: 1.0, y: 2.0 }`            | struct                                         |
| sequence of struct maps         | array of struct                                |
| struct map containing sequences | struct with array member                       |

v1.1 notes:

- Nested aggregates (arbitrary composition of arrays and structs) are
  fully supported on both the input and output sides.
- **Struct literals must be full.** Every field of a struct expected or
  input must appear. Missing or unknown keys fail load. Partial literals
  are planned for a future release.
- Inline tables above a size threshold must come from a sidecar CSV or
  EXR via `from_file:` -- raw in-YAML tables of hundreds of entries are
  unreadable on diff. (Threshold is currently a soft ~64 elements.)
- Tolerance keys (`abs`, `rel`, `ulp`) on non-FP leaves (bool / int /
  string) are load-time errors.

## Tolerance block

```yaml
tolerance:
  abs: 1.0e-6
  rel: 1.0e-6
  ulp: 4                       # native precision unless image mode; see below
  per_field:                   # dotted / bracketed path overrides
    return.L:   { abs: 0.5 }
    out[2].b:   { ulp: 1 }
  per_channel:                 # image mode only; keyed by EXR channel name
    R:          { ulp: 2 }
```

Semantics summary (see [`TOLERANCE.md`](./TOLERANCE.md) for the long
form):

- **Pass rule**: a leaf passes if any of the declared `abs`/`rel`/`ulp`
  bounds is satisfied (OR, not AND).
- **Merge order**: `defaults.tolerance` to `tests[].tolerance` to matching
  `per_field` / `per_channel` entry on the current path.
- **`per_field` paths** use `.` for struct-member descent and `[i]` for
  sequence indexing: `return.v[2]`, `q[0].x`.
- **`per_channel`** applies only in image mode and is keyed by EXR
  channel name (`R`, `G`, `B`, `A`, ...).
- **Image mode + `ulp:` requires `image.ulp_precision`** (`float`
  currently; `half` reserved; `native` reserved). Omitting it when any
  `ulp:` tolerance is in effect is a load-time error.

## Oracle block

Exactly one of `inline`, `csv`, `exr`, `snapshot` must be present.

### `inline`

Map from output-arg name (or `returns_name`) to expected Value. Every
non-defaulted output must appear or be listed in `ignore_outputs:`.

```yaml
oracle:
  inline:
    return: 3.0
    out_hi: [1.0, 2.0]
    out_struct:
      L: 48.0
      a: 0.0
      b: 0.0
```

### `csv`

Path to a CSV whose columns map to output-arg names. Row N of the inputs
CSV pairs with row N of this CSV (sweep mode only).

```yaml
oracle:
  csv: csv/expected.csv       # resolved vs YAML dir
```

### `exr`

Path to a reference EXR. The set of channels in the reference drives the
compare; extras on the actual side are flagged. Pairs with `mode: image`.

```yaml
oracle:
  exr: references/macbeth_rec709_ref.exr
  max_failing_pixels: 2        # cap on per-pixel, per-channel failures before the case fails
```

- `max_failing_pixels: 0` (default): any mismatch fails.
- `max_failing_pixels: N > 0`: up to N channel-pixels may mismatch.
- `max_failing_pixels: -1`: no cap (diagnostic-only).

### `snapshot`

Two shapes accepted:

```yaml
# Short form: read-only
oracle:
  snapshot: snapshots/lut.yaml

# Explicit form with per-test write opt-in
oracle:
  snapshot:
    path: snapshots/lut.yaml
    writable: true
```

Three-gate write protection:

1. **Env**: `CTL_TEST_UPDATE_SNAPSHOTS=1` (or `=force`) -- equivalently
   `ctltest --update-snapshots[=force]`.
2. **Per-test**: `snapshot.writable: true` -- even with the env gate set,
   a test without this key never writes.
3. **First-time record**: `CTL_TEST_ALLOW_NEW_SNAPSHOTS=1` -- equivalently
   `ctltest --allow-new-snapshots`. Required only when the snapshot file
   does not yet exist. This prevents CI silently greening a test whose
   expected outputs have never been recorded anywhere.

See [`CLI.md`](./CLI.md) "Snapshot flags" for typical workflows.

## Sweep block

```yaml
mode: sweep
function: mymodule::clamp_ramp
sweep:
  inputs: csv/inputs.csv       # path resolved vs YAML dir
  strict: true                 # default. extra/missing columns fail load. false to stderr warning.
oracle:
  csv: csv/expected.csv
```

- Column names in the inputs CSV match CTL parameter names.
- Column names in the expected CSV match output-arg names plus
  `returns_name` if the function has a return value.
- `strict: true` (default) fails load if inputs-CSV columns don't line
  up with the CTL parameter list. `strict: false` downgrades to a stderr
  warning.

## Image block

```yaml
mode: image
function: mymodule::pixel_op
image:
  input: fixtures/input.exr
  input_channels:              # optional; default = pass-through by name
    R_in: R
    G_in: G
    B_in: B
  output_channels:             # optional; default = pass-through by name
    R: R_out
    G: G_out
    B: B_out
  ulp_precision: float         # required if tolerance uses ulp:
oracle:
  exr: references/output_ref.exr
  max_failing_pixels: 2
```

- `input_channels` maps CTL input-arg name to EXR source channel name.
- `output_channels` maps EXR destination channel name to CTL output-arg
  name.
- Omitting either map makes the runner try a natural pass-through by
  shared names (`R` to `R`, `G` to `G`, ...).
- `ulp_precision` is currently only `float`. `half` is reserved; `native`
  is reserved. Present-but-unrecognized values fail load.

## `ctl_native` mode

```yaml
mode: ctl_native
function: test_mymodule::test_identity_is_identity
```

- The CTL function must be zero-argument and return `void`.
- The function body uses `testkit::expect_*` to record assertions.
- `inputs:` and `oracle:` are both forbidden in this mode.
- See [`TESTKIT_API.md`](./TESTKIT_API.md) for the full CTL-side surface.

## Path resolution rules

Any filesystem path in the YAML (`module_paths[]`, `sweep.inputs`,
`oracle.csv`, `oracle.exr`, `oracle.snapshot.path`, `image.input`) is:

- **absolute** if it starts with `/` -- used verbatim.
- **relative** otherwise -- resolved against the directory of the YAML
  file being loaded, then normalized.

## Complete example

See [`examples/`](../examples/) for worked cases:

- `aces_output_transform.yaml` -- all five modes in one file.
- `csv_sweep.yaml` -- parameterized sweep.
- `snapshot_approval.yaml` -- three-gate write protection.
- `escape_hatch/test_mymodule.ctl` + `ctl_tests.yaml` -- CTL-native.

## Not yet supported (will load-error)

v1.1 deliberately rejects the following so authors don't silently get
the wrong behavior:

- **Partial struct literals.** Every member must appear.
- **Varying struct outputs** in sweep/image modes. Load-error message:
  `"not yet supported"`. Lift planned once SimdReg layout is verified.
- **`ulp_precision: half`** in image mode. Authors should use `float`
  and convert tolerances accordingly.
- **Tolerance keys on non-FP leaves.** Ints / bools / strings compare
  exactly. `normalize:` on strings is planned for v2.
