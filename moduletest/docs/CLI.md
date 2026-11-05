# `ctltest` CLI reference

`ctltest` is the standalone driver installed alongside `ctlrender`. It
loads YAML suites (or walks directories for them), runs every case, and
writes a reporter stream to stdout or a file.

For the in-tree ctest integration, see
[`ARCHITECTURE.md`](./ARCHITECTURE.md) "ctest integration".

---

## Synopsis

```
ctltest [options] <path> [<path> ...]
```

Each `<path>` is either a YAML suite file or a directory. Directories
are walked for `*.yaml` / `*.yml` (recursively). A subdirectory named
`snapshots` is skipped -- snapshot files look like suite files but aren't.

Results from multiple paths are aggregated into one reporter stream.

## Exit code

- `0` if every case passed (or was a legitimate `skipped`).
- `1` if any case ended in `failed`, `errored`, or `unexpectedPass`.
- `2` on usage error (unknown flag, missing value, no inputs, unreadable
  output file).

`errored` includes YAML load errors, missing modules, and uncaught CTL
runtime exceptions.

## Options

### `--reporter {console|tap|junit}`

Output format. Default: `console`.

- `console` -- human-readable, colorized when stdout is a TTY. Multi-line
  diagnostics include the dotted path, expected/got text, observed
  errors, and which tolerance was in effect.
- `tap` -- TAP v14 stream. One `ok` / `not ok` line per case, with YAML
  diagnostic blocks for failures. Ends with a `1..N` plan line.
- `junit` -- single JUnit XML document covering all suites. Emitted
  after all cases run (the whole document is one `finalize()` call).
  Pair with `--output` to capture directly to a file.

### `--output FILE`

Write reporter output to `FILE` instead of stdout. Errors (load errors,
usage text) still go to stderr. Overwrites `FILE` if it exists.

### `--filter GLOB`

Include only cases whose `id:` matches `GLOB` (shell-style: `*` and `?`).
The filter applies per-case, not per-suite; the suite is still loaded
and iterated, non-matching cases are just skipped.

Examples:

```
ctltest --filter 'pure_*' tests/          # everything starting with "pure_"
ctltest --filter '*_oob'  tests/          # out-of-band cases only
ctltest --filter 'hue_*'  tests/aces.yaml # single-suite scope
```

### `--no-color`

Disable ANSI color in the console reporter. Use for log capture or when
the TTY check gets the wrong answer.

### Snapshot flags

See [`YAML_SCHEMA.md`](./YAML_SCHEMA.md) "Snapshot block" for the three-
gate scheme. These flags set env vars the SnapshotOracle reads directly:

- `--update-snapshots` sets `CTL_TEST_UPDATE_SNAPSHOTS=1`. With a
  per-test `writable: true` gate, mismatches get overwritten instead of
  failing.
- `--update-snapshots=force` sets `CTL_TEST_UPDATE_SNAPSHOTS=force`.
  Equivalent to `=1` in current behavior but kept as a distinct token so
  future releases can add "also overwrite read-only snapshots" semantics.
- `--allow-new-snapshots` sets `CTL_TEST_ALLOW_NEW_SNAPSHOTS=1`. Needed
  the *first* time a snapshot is recorded.

The per-test `writable: true` gate in YAML still applies and is not
overridable from the command line.

### `-h`, `--help`

Print usage to stderr and exit 0.

## Environment variables

| Variable                       | Effect |
|--------------------------------|--------|
| `CTL_TEST_UPDATE_SNAPSHOTS`    | Allow SnapshotOracle to write. Accepts `1` / `true` / `on` / `yes` / `force`. |
| `CTL_TEST_ALLOW_NEW_SNAPSHOTS` | Allow first-time snapshot record. Same truthy values. |

The CLI flags above are just shortcuts for setting these. Invoking
`ctltest` from a shell where the vars are already set has the same
effect as the flags -- this is intentional so CI can opt whole jobs into
a mode via env.

## Typical invocations

Daily CI over the whole suite directory with TAP output:

```
ctltest --reporter tap tests/
```

Capture JUnit XML for a specific suite:

```
ctltest --reporter junit --output junit.xml tests/aces_output.yaml
```

Bootstrap snapshots the first time:

```
ctltest --update-snapshots --allow-new-snapshots tests/snapshots.yaml
```

Intentionally overwrite drift on a snapshot suite:

```
ctltest --update-snapshots=force tests/snapshots.yaml
```

Run just one case:

```
ctltest --filter 'scale_by_two' tests/mymodule_test.yaml
```

## Interaction with ctest

In the in-tree build, each entry in `moduletest/tests/manifest.txt`
becomes an `add_test(NAME "ctltest::<rel>" ...)` invocation of the
internal `ctltest_run_one` binary (not `ctltest`). Running the installed
`ctltest` CLI on the same YAMLs gives identical results; the two
binaries share `ctltest_core`.

Labels:

- `ctltest` -- the YAML-driven self-test and registered-suite cases.
- `ctltest-unit` -- the C++ unit-test binary (`ctltest_unit`) covering
  ctltest_core's own public API.

```
ctest --test-dir build -L ctltest
ctest --test-dir build -L 'ctltest|ctltest-unit'   # everything ctltest owns
```

## Diagnostics in console reporter output

A failing scalar diff looks like:

```
  [ ] scale_by_two
      return: expected 3.1, got 3
        abs_err = 0.1
        tolerance = abs:1e-06 (suite.default)
```

Fields:

- **path** -- dotted / bracketed path into the output tree. `return`,
  `out.r`, `out[2].g`. See [`YAML_SCHEMA.md`](./YAML_SCHEMA.md)
  "Tolerance block".
- **expected / got** -- human text from `Value::describe()`. For halves,
  both the authored decimal and the 16-bit pattern appear.
- **abs_err / rel_err / ulp_err** -- all three are computed and reported.
  The one your tolerance used is highlighted.
- **tolerance** -- the effective bound plus its source
  (`suite.default` / `test.tolerance` /
  `test.tolerance.per_field[out.b]`).

## Known limitations

- No glob in `--output` -- a single file or stdout.
- No parallel-case execution inside a suite yet; each case sets up its
  own interpreter and runs serially.
- No per-case timeout; a runaway CTL function stalls the run.
- `--reporter junit` buffers the full document until the end. Extremely
  large runs will hold diagnostics in memory.
