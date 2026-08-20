# Tolerance semantics

ctltest lets an author attach tolerance rules to any floating-point
leaf. This document explains how those rules compose, how half
precision is handled, and where the surprising edges are.

For the YAML key reference see
[`YAML_SCHEMA.md`](./YAML_SCHEMA.md) "Tolerance block".

---

## The three bounds

A `Tolerance` carries up to three optional bounds:

| Key   | Meaning                                         |
|-------|-------------------------------------------------|
| `abs` | `|got - expected| <= abs`                       |
| `rel` | `|got - expected| / |expected| <= rel`          |
| `ulp` | ULP distance between the two floats `<= ulp`    |

Plus two forms of local override:

- `per_field: { path: { ... } }` -- overrides for a leaf keyed by dotted
  path (e.g. `return.L`, `out[2].b`).
- `per_channel: { name: { ... } }` -- image-mode only, keyed by EXR
  channel name (`R`, `G`, ...).

## The "pass if any satisfies" rule

A floating-point leaf passes if **any** of the declared bounds is
satisfied. This is deliberately OR, not AND.

Example:

```yaml
tolerance:
  abs: 1.0e-6
  rel: 1.0e-5
  ulp: 4
```

Any leaf that falls within any one of those bounds passes. You get the
tightest useful bound for typical inputs (`abs` for values near zero,
`rel` for large values, `ulp` for pathological libm drift on small
values) without authoring three separate tests.

Edge: if no bound is declared at all on a non-zero diff, the leaf
fails. Bit-identical values (including +/-0) short-circuit and pass
regardless of any bounds.

## Merge order

Tolerance is resolved per-leaf, walking the dotted path. At each level,
a more specific tolerance overrides broader ones by **field replacement**
(not field union):

1. `suite.defaults.tolerance` (the base)
2. `tests[i].tolerance` -- `Tolerance::merge(suite_default, test_tol)`
3. `tests[i].tolerance.per_field[<current path>]` -- sharpens further

Each `merge(base, override)` replaces a field only if the override has
it set. So:

```yaml
# Suite default:
defaults:
  tolerance:
    abs: 1.0e-6
    rel: 1.0e-6

# Per-test override:
tests:
  - id: loose_integration
    tolerance:
      abs: 1.0e-3          # abs replaced
      # rel not mentioned   # rel stays at 1.0e-6
    ...
```

At `test.tolerance[loose_integration]` the effective bound is
`abs: 1e-3, rel: 1e-6`. Both still apply under the OR rule.

`per_field` follows the same merge rule, with the base coming from
whatever was in effect when we arrived at that leaf.

Every reported diagnostic includes a `toleranceSource` string so you
can trace which layer actually applied:

- `suite.default`
- `test.tolerance`
- `test.tolerance.per_field[out.b]`

## Path syntax in `per_field`

Paths use `.` to descend into struct members and `[i]` to index into
sequences:

```yaml
per_field:
  return.L:           { abs: 0.5 }      # struct member
  out[2]:             { abs: 1.0e-4 }   # array cell
  q[0].x:             { rel: 1.0e-6 }   # array of struct
  return.v[2]:        { ulp: 1 }        # struct with array member
```

A path that doesn't resolve in the expected shape is a load-time error
(`validateTolerancePaths` check). "Doesn't resolve" covers unknown keys,
out-of-range indices, and intermediate kind mismatches.

v1.1 made the full multi-segment path work -- v1.0 would silently stop
after the first segment (see RELEASE_NOTES "Interpreter fix"). If you
had to flatten out a `return.v[2]` workaround in v1.0, v1.1 accepts it
natively.

## Non-FP leaves

Tolerance bounds (`abs` / `rel` / `ulp`) on non-FP leaves are a load-
time error. Specifically, attaching `{ abs: ... }` to a `per_field` path
that resolves to:

- `bool`
- `int` / `unsigned int`
- `string`

fails `validateTolerancePaths` at load time. Integer and string
comparison is always exact; there is no tolerance for "almost true".

If you need loose string compare (case-insensitive, whitespace-trimmed),
the plan defers that to a `normalize: {trim, case_insensitive}` key in a
future release. It is not in v1.1.

## Half precision

CTL `half` is 16-bit IEEE 754. When an author writes `return: 0.1` in
YAML and the CTL function returns `half`, the two values cannot be bit-
identical -- `0.1` is not exactly representable in either float or half.

ctltest handles this by **rounding the authored value to nearest-even
half once on compare**. Against a `Kind::Half` leaf the comparison is:

```
half  hExp = static_cast<float>(author_value);   // round-to-nearest-even
float evHalf = static_cast<float>(hExp);         // exact upcast
compareFloat(evHalf, got.f, tol, ...);
```

The diagnostic renders both forms:

- the authored decimal (`0.1`)
- the rounded-half bit pattern (`0x2e66` becomes `0.099976`)

so authors can reason about sub-ULP drift against the actual
representable half.

## ULP semantics

ULP is computed on `float32`. The formula saturates at `INT64_MAX` for
NaN / infinity on either side; those are treated as "non-matching"
unless bit-identical.

`ulp: N` in isolation means "pass if the ULP distance is <= N in
float32".

In **image mode** any use of `ulp:` requires an explicit
`image.ulp_precision` key so authors don't silently interpret a
`ulp: 1` bound as half-ULP when the EXR happens to be 16-bit float.
`float` (the v1 default) is the only accepted value today; `half` is
reserved; `native` is reserved.

For unit / sweep / snapshot oracles the pixel-type question doesn't
arise -- `ulp:` is always float32 ULP.

## Image mode extras

Image oracles add one more tolerance surface:

```yaml
tolerance:
  abs: 1.0e-4
  per_channel:
    R: { ulp: 2 }
    A: { abs: 0.0 }     # alpha is authored, must be exact
image:
  ulp_precision: float
oracle:
  exr: ref.exr
  max_failing_pixels: 2
```

- `per_channel` is keyed by EXR channel name (`R`, `G`, `B`, `A`, ...).
  It merges on top of the test-level tolerance.
- `max_failing_pixels` sets a cap on per-pixel, per-channel failures
  before the case is declared failing. Useful for edge-of-gamut
  rasterization tests where 1-2 boundary pixels drift.
  - `0` (default) -- any mismatch fails.
  - `N > 0` -- up to N channel-pixels may fail.
  - `-1` -- diagnostic-only; the case can't fail on pixel drift.

## Common idioms

**"Tight by default, loose on one hot spot"**:

```yaml
tolerance:
  abs: 1.0e-7
  per_field:
    return.highlight: { abs: 1.0e-3 }    # headroom drift
```

**"Relative for large values, absolute near zero"**:

```yaml
tolerance:
  abs: 1.0e-6
  rel: 1.0e-6
```

Under the OR rule, `abs` catches values near zero (where `rel` is
undefined or huge), `rel` catches large values (where `1e-6 abs` is
pointlessly tight).

**"Known libm-drift slop via ULPs"**:

```yaml
tolerance:
  ulp: 4
```

Four float ULPs is ~2.4e-7 near 1.0 -- tight enough to catch logic
errors, loose enough to absorb vendor libm differences on exp/log/pow.

**"Force exact on integer-like outputs in a mostly-FP struct"**:

```yaml
tolerance:
  abs: 1.0e-6
  per_field:
    return.count: {}        # empty -- no bounds; compare exactly
```

Integer leaves compare exactly regardless of what `abs`/`rel`/`ulp` are
set to, so the override is strictly cosmetic here; omitting the
per_field entry has the same effect.

## Further reading

- [`lib/Oracle.cc`](../lib/Oracle.cc) -- `compareFloat` and `walk` carry
  the canonical semantics; each branch of `Value::Kind` documents what
  it does.
- [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) "Tolerance block" -- the user-
  facing authoring syntax.
