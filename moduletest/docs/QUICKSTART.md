# ctltest quickstart

Your first test in five minutes. This walks through a single scalar CTL
function, a minimal YAML suite, and running under both `ctest` and the
standalone `ctltest` CLI.

If you want the full schema after this, see
[`YAML_SCHEMA.md`](./YAML_SCHEMA.md). For the CTL-native escape hatch see
[`TESTKIT_API.md`](./TESTKIT_API.md). For tolerance semantics see
[`TOLERANCE.md`](./TOLERANCE.md).

---

## 1. Lay out the files

```
my_project/
  mymodule.ctl
  tests/
    mymodule_test.yaml
```

## 2. The CTL module

```ctl
// mymodule.ctl
namespace mymodule
{

float
scale(float x, float k)
{
    return x * k;
}

} // namespace mymodule
```

## 3. The YAML suite

```yaml
# tests/mymodule_test.yaml
version: 1
suite: mymodule

modules:
  - mymodule

module_paths:
  - ..              # path relative to this YAML file's directory

defaults:
  tolerance:
    abs: 1.0e-6

tests:
  - id: scale_by_two
    description: scale(1.5, 2.0) == 3.0
    function: mymodule::scale
    inputs:
      x: 1.5
      k: 2.0
    oracle:
      inline:
        return: 3.0
```

Three things to notice:

- `modules:` lists CTL modules to load; they're resolved against
  `module_paths`, each of which is resolved relative to the YAML file.
- `function:` is `module::name`. `inputs:` is keyed by CTL parameter name.
- `oracle.inline` is keyed by output arg name. The return value is
  addressed as `return` by default (override with `returns_name:`).

## 4. Run it

**Standalone CLI** (installed alongside `ctlrender`):

```
ctltest tests/mymodule_test.yaml
```

Expected output:

```
suite: mymodule (1 cases)
  [x] scale_by_two
1 passed, 0 failed, 0 errored
```

**In a ctest-driven build** -- add the suite path to
`moduletest/tests/manifest.txt` and regenerate:

```
cmake --build build
ctest --test-dir build -L ctltest
```

Each manifest entry becomes one `ctltest::<path>` test.

## 5. See a failure

Edit `oracle.inline.return` to `3.1` and rerun. Console output:

```
[ ] scale_by_two
    return: expected 3.1, got 3
      abs_err = 0.1
      tolerance = abs:1e-06 (suite.default)
1 passed => 0 passed, 1 failed
```

Every diagnostic includes the dotted path, the expected/got text, the
observed error, and which tolerance was in effect and where it came from.

## 6. Where to go next

- **More inputs than you want to hand-author**: move to a CSV sweep. See
  `examples/csv_sweep.yaml` and the `sweep:` section of
  [`YAML_SCHEMA.md`](./YAML_SCHEMA.md).
- **EXR-in, EXR-out conformance**: see `examples/aces_output_transform.yaml`
  (test 4) and the `image:` section of [`YAML_SCHEMA.md`](./YAML_SCHEMA.md).
- **A function that produces too many outputs to author by hand**: use
  snapshot/approval mode. See `examples/snapshot_approval.yaml`.
- **Loops, property checks, intermediate-value assertions**: CTL-native
  escape hatch. See [`TESTKIT_API.md`](./TESTKIT_API.md).
- **Structured CI output**:
  ```
  ctltest --reporter tap    tests/
  ctltest --reporter junit --output junit.xml tests/
  ```
  See [`CLI.md`](./CLI.md) for the full flag reference.

## Tips for authoring from here

- Start with `oracle.inline` + tight `abs`. Loosen only when you hit real
  libm drift; the first failure diagnostic already reports the ULP error,
  so you can decide based on observation instead of guessing.
- Full struct literals only in v1. Every field of a struct input/output
  must appear. See [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) "Values & types".
- Snapshot mode is gated. First recording needs both the env var and a
  per-test `writable: true`; see [`CLI.md`](./CLI.md) "Snapshot flags".
