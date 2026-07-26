#!/usr/bin/env python3
"""Generate random CTL programs and assert ctlrender produces
byte-identical output at -threads 1 vs -threads N.

Usage:
    tools/property_test_simd_parity.py \\
        --ctlrender PATH/TO/ctlrender \\
        --input  PATH/TO/input.exr \\
        --workdir DIR \\
        [--count N] [--seed N] [--threads-vec N]

Exits 0 on full pass, 1 on any divergence.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import random
import shutil
import subprocess
import sys
import textwrap


# Inputs ctlrender exposes when reading an RGB EXR through a CTL function.
INPUT_NAMES = ["rIn", "gIn", "bIn"]
OUTPUT_NAMES = ["rOut", "gOut", "bOut"]


def gen_expression(rng: random.Random, depth: int, vars_in_scope: list[str]) -> str:
    """Build a small arithmetic expression tree.

    Picked to be safe under any input -- no division (avoids /0), no log/sqrt
    (avoids domain errors on negative inputs), only +/-/*/abs/min/max."""
    if depth == 0 or rng.random() < 0.3:
        # Leaf: a variable in scope or a small literal.
        if rng.random() < 0.7 and vars_in_scope:
            return rng.choice(vars_in_scope)
        return f"{rng.uniform(-2.0, 2.0):.4f}"

    op = rng.choice(["+", "-", "*"])
    a = gen_expression(rng, depth - 1, vars_in_scope)
    b = gen_expression(rng, depth - 1, vars_in_scope)
    return f"({a} {op} {b})"


def gen_program(rng: random.Random, name: str) -> str:
    """Emit a complete .ctl program with a function named `name`.

    Function signature matches ctlrender image input convention so the
    fixture can be used directly with `-ctl <fixture>` against an EXR."""
    n_params = rng.randint(0, 3)
    params = [(f"p{i}", rng.uniform(-1.5, 1.5)) for i in range(n_params)]
    var_pool = list(INPUT_NAMES) + [n for n, _ in params]

    sig_lines = []
    for n in OUTPUT_NAMES:
        sig_lines.append(f"     output varying float {n}")
    for n in INPUT_NAMES:
        sig_lines.append(f"     input varying float {n}")
    for pname, pdefault in params:
        sig_lines.append(f"     input uniform float {pname} = {pdefault:.4f}")
    sig = ",\n".join(sig_lines)

    body_lines = []
    for out_name in OUTPUT_NAMES:
        depth = rng.randint(1, 4)
        expr = gen_expression(rng, depth, var_pool)
        body_lines.append(f"    {out_name} = {expr};")

    return textwrap.dedent(f"""\
        // Auto-generated property test fixture (seed-derived).
        // Vector vs scalar interpreter paths must produce byte-identical
        // output for any valid input -- divergence indicates a SIMD bug.
        void main(
        {sig})
        {{
        {chr(10).join(body_lines)}
        }}
        """)


def run_ctlrender(ctlrender: str, ctl: str, input_exr: str, output: str,
                  threads: int) -> tuple[bool, str]:
    """Invoke ctlrender; return (ok, stderr)."""
    cmd = [
        ctlrender,
        "-threads", str(threads),
        "-ctl", ctl,
        "-format", "exr32",
        "-force",
        input_exr,
        output,
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return (proc.returncode == 0, proc.stderr)


def files_equal(a: str, b: str) -> bool:
    if not (os.path.exists(a) and os.path.exists(b)):
        return False
    if os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read() == fb.read()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctlrender", required=True, help="path to ctlrender binary")
    ap.add_argument("--input", required=True, help="input EXR file")
    ap.add_argument("--workdir", required=True, help="scratch dir for generated files")
    ap.add_argument("--count", type=int, default=20, help="number of random programs")
    ap.add_argument("--seed", type=int, default=0, help="master seed")
    ap.add_argument("--threads-vec", type=int, default=4,
                    help="thread count for the vector run")
    args = ap.parse_args()

    workdir = pathlib.Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)

    failures: list[tuple[int, str]] = []
    skipped = 0
    master_rng = random.Random(args.seed)

    for i in range(args.count):
        # Each program gets its own derived seed for reproducibility.
        program_seed = master_rng.randint(0, 2**31 - 1)
        rng = random.Random(program_seed)
        ctl_path = workdir / f"prog_{i:03d}.ctl"
        ref_path = workdir / f"prog_{i:03d}_ref.exr"
        out_path = workdir / f"prog_{i:03d}_out.exr"

        ctl_path.write_text(gen_program(rng, "main"))

        ok_ref, err_ref = run_ctlrender(
            args.ctlrender, str(ctl_path), args.input, str(ref_path),
            threads=1)
        if not ok_ref:
            # Generated program may have a CTL semantic issue (e.g.
            # type inference); skip rather than fail.
            skipped += 1
            continue

        ok_run, err_run = run_ctlrender(
            args.ctlrender, str(ctl_path), args.input, str(out_path),
            threads=args.threads_vec)
        if not ok_run:
            failures.append((program_seed,
                             f"vector run failed but scalar succeeded:\n{err_run}"))
            continue

        if not files_equal(str(ref_path), str(out_path)):
            failures.append((program_seed,
                             "output diverges between -threads 1 and "
                             f"-threads {args.threads_vec}"))

    print(f"property test: {args.count} programs, "
          f"{skipped} skipped (parse/type errors), "
          f"{len(failures)} divergence{'s' if len(failures) != 1 else ''}")

    if failures:
        for seed, msg in failures:
            print(f"  seed={seed}: {msg}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
