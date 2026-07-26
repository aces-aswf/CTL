#!/usr/bin/env python3
"""Apply each mutation in MUTATIONS, incrementally rebuild + run tests,
report survivors (mutations no test caught).

Mutations are hand-curated, not generated, so the set is small and
each entry can target a specific invariant.

Usage:
    tools/mutation_test_simd.py --build-dir /path/to/build [--test-filter REGEX]

Exits 0 if every mutation was caught, 1 if any survived.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


@dataclasses.dataclass
class Mutation:
    file: str            # path relative to repo root
    pattern: str         # regex that uniquely matches the line to mutate
    replacement: str     # replacement string
    description: str     # human-readable description
    target: str          # cmake target to (re)build before running tests


# Each `pattern` MUST uniquely match a single line in `file`; the
# harness aborts a mutation whose pattern matches 0 or >1 times.
MUTATIONS: list[Mutation] = [
    # ---------------------------------------------------------------
    # Group A: SimdReg / SimdBoolMask register-class invariants
    # ---------------------------------------------------------------

    # SimdBoolMask::setVarying widening: memset broadcasts inline value across
    # all lanes.  Mutate the broadcast value to a constant 0 -- testSimdRegAddr
    # checks lane[i] == seed after widen.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdReg.h",
        pattern=r"memset\(data, _data\[0\], MAX_REG_SIZE\);",
        replacement="memset(data, 0, MAX_REG_SIZE);",
        description="SimdBoolMask widen: broadcast 0 instead of inline value",
        target="IlmCtlTest",
    ),
    # SimdBoolMask::setVarying narrow: should preserve _data[0] in
    # _inlineData.  Mutate to preserve _data[1] -- testSimdRegAddr's
    # strengthened lane-discrimination check pins this.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdReg.h",
        pattern=r"_inlineData = _data\[0\];",
        replacement="_inlineData = _data[1];",
        description="SimdBoolMask narrow: preserve lane[1] instead of lane[0]",
        target="IlmCtlTest",
    ),
    # SimdReg::reference: assignment delete[]s old data before reassignment.
    # Skipping the delete leaks memory but doesn't break observable values.
    # Documents an inherent limit of unit-test mutation testing -- only an
    # ASan build catches this class.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdReg.cpp",
        pattern=r"if \(_dataOwned\)\n\tdelete \[\] _data;\n\n    //",
        replacement="if (_dataOwned)\n\t{ /* leak: _data; */ }\n\n    //",
        description="reference(): leak _data instead of deleting (ASan-only catch)",
        target="IlmCtlTest",
    ),

    # ---------------------------------------------------------------
    # Group B: interpreter dispatch invariants
    # ---------------------------------------------------------------

    # SimdInterpreter::maxSamples returns MAX_REG_SIZE.  Mutate to half.
    # testConcurrentCalls REQUIRE()s shape.samples <= maxSamples for every
    # shape (8192 specifically); halving breaks that.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdInterpreter.cpp",
        pattern=r"return MAX_REG_SIZE;",
        replacement="return MAX_REG_SIZE / 2;",
        description="maxSamples() returns half MAX_REG_SIZE",
        target="IlmCtlTest",
    ),

    # ---------------------------------------------------------------
    # Group C: SimdInst branch + loop semantics
    # ---------------------------------------------------------------

    # SimdBranchInst::execute fuses trueMask/falseMask construction with
    # `t = mi & ci` (active lane AND condition true).  Replacing with
    # plain `ci` ignores the active-lane mask -- branches that should be
    # masked-off lane-by-lane silently take the true path.  Catches an
    # entire category of "mask not respected" regressions.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdInst.cpp",
        pattern=r"const bool t  = mi & ci;",
        replacement="const bool t  = ci;",
        description="SimdBranchInst: ignore mask in trueMask construction",
        target="IlmCtlTest",
    ),

    # Symmetric: falseMask branch ignores the active-lane mask.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdInst.cpp",
        pattern=r"const bool f  = mi & !ci;",
        replacement="const bool f  = !ci;",
        description="SimdBranchInst: ignore mask in falseMask construction",
        target="IlmCtlTest",
    ),

    # SimdBranchInst neither-branch lanes get memset to 0; flip to 1
    # so neither-branch lanes write a non-zero bit pattern.  Caught by
    # any test that takes a varying-condition branch.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdInst.cpp",
        pattern=r"memset \(\(\*outReg\)\[i\], 0, eSize\);",
        replacement="memset ((*outReg)[i], 1, eSize);",
        description="SimdBranchInst: neither-branch lanes get pattern 0x01 not 0x00",
        target="IlmCtlTest",
    ),

    # SimdLoopInst conditional-mask narrowing: original ANDs the loop
    # mask with the condition (lanes that became false drop out).
    # Mutating to plain `=` re-enables previously-masked lanes whenever
    # the condition is true -- runaway iterations on lanes that should
    # have stopped.  Property-based test should catch this.
    Mutation(
        file="lib/IlmCtlSimd/CtlSimdInst.cpp",
        pattern=r"loopMask\[i\] \&= \*\(bool\*\)\(condition\[i\]\);",
        replacement="loopMask[i] = *(bool*)(condition[i]);",
        description="SimdLoopInst: condition mask assignment forgets prior mask state",
        target="IlmCtlTest",
    ),

    # ---------------------------------------------------------------
    # Group D: ctlrender threading + parallel float->half
    # ---------------------------------------------------------------

    # transform.cc tile fetch_add: `next_tile.fetch_add(1, ...)` claims
    # one tile per worker.  Replacing with `.load(...)` makes every
    # worker see the same idx -- multiple workers process the same tile
    # while later tiles are skipped.  Caught by the threaded-parity
    # cmp tests (output diverges from -threads 1 reference).
    Mutation(
        file="ctlrender/transform.cc",
        pattern=r"size_t idx = next_tile\.fetch_add\(1, std::memory_order_relaxed\);",
        replacement="size_t idx = next_tile.load(std::memory_order_relaxed);",
        description="transform.cc: tile fetch_add becomes load (workers race on same tile)",
        target="ctlrender",
    ),

    # transform.cc worker_count autoselect: original uses
    # `if (hw > 1) worker_count = hw;`.  Mutating to `worker_count = 1`
    # (single-thread autoselect) means -threads 0 silently runs serial.
    # Caught by ctlrender-threads-autoselect-cmp (byte parity vs
    # -threads 1 reference; would actually still match because both are
    # serial -- DOCUMENTED as expected survivor for this reason).
    Mutation(
        file="ctlrender/transform.cc",
        pattern=r"if \(hw > 1\) worker_count = hw;",
        replacement="worker_count = 1;",
        description="transform.cc: -threads 0 autoselect always serial (expected SURVIVOR -- byte-parity equivalent)",
        target="ctlrender",
    ),

    # exr_file.cc parallel float->half: each worker grabs `kChunk`
    # elements via fetch_add.  Mutating to grab `kChunk - 1` means each
    # chunk loses its last element to the half(0.0f) default-construction
    # of the std::vector<half> output buffer's tail.  Wait -- output
    # is half_pixels.ptr() (raw memory), not zero-initialised, so the
    # last element of each chunk gets uninitialised garbage.  Caught
    # by ctlrender-parallel-half-cmp-* (byte parity).
    Mutation(
        file="ctlrender/exr_file.cc",
        pattern=r"uint64_t end = begin \+ kChunk;",
        replacement="uint64_t end = begin + kChunk - 1;",
        description="exr_file.cc parallel half: chunk one element short",
        target="ctlrender",
    ),

    # exr_file.cc parallel float->half: replace the conversion body with
    # a hard-coded zero output.  Trivially caught by every parallel-half
    # test, but useful as a sanity check that the harness is wired up.
    Mutation(
        file="ctlrender/exr_file.cc",
        pattern=r"out\[i\] = half\(fIn\[i\]\);",
        replacement="out[i] = half(0.0f);",
        description="exr_file.cc parallel half: emit zero instead of converted value",
        target="ctlrender",
    ),
]


def find_unique_match(content: str, pattern: str) -> tuple[int, int] | None:
    """Return (start, end) of the unique regex match, or None if not unique."""
    matches = list(re.finditer(pattern, content))
    if len(matches) != 1:
        return None
    return matches[0].span()


def apply_mutation(file_path: pathlib.Path, m: Mutation) -> str:
    """Apply mutation; return the original file content for restoration."""
    original = file_path.read_text()
    span = find_unique_match(original, m.pattern)
    if span is None:
        raise RuntimeError(
            f"mutation pattern matched 0 or >1 times in {m.file}: {m.pattern}")
    start, end = span
    mutated = original[:start] + m.replacement + original[end:]
    file_path.write_text(mutated)
    return original


def restore(file_path: pathlib.Path, original: str) -> None:
    file_path.write_text(original)


def run_build(build_dir: pathlib.Path, target: str) -> tuple[bool, str]:
    proc = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", target, "-j"],
        capture_output=True, text=True)
    return (proc.returncode == 0, proc.stderr or proc.stdout[-2000:])


def run_tests(build_dir: pathlib.Path, test_filter: str,
              timeout_s: int = 60) -> tuple[bool, str]:
    """Run the test suite with a hard timeout -- a mutation that creates an
    infinite loop must not hang the harness.  Treat timeout as 'caught'
    (the mutation produced observable wrong behaviour: a hang)."""
    cmd = ["ctest", "--timeout", str(timeout_s)]
    if test_filter:
        cmd += ["-R", test_filter]
    try:
        proc = subprocess.run(
            cmd, cwd=str(build_dir), capture_output=True, text=True,
            timeout=timeout_s * 4)
    except subprocess.TimeoutExpired:
        return (False, "harness timeout -- mutation likely caused a hang")
    return (proc.returncode == 0, proc.stdout[-2000:])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True,
                    help="cmake build dir (must already be configured)")
    ap.add_argument("--test-filter", default="",
                    help="ctest -R regex (default: all tests)")
    args = ap.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    if not (build_dir / "CMakeCache.txt").exists():
        print(f"error: {build_dir} is not a configured cmake build dir",
              file=sys.stderr)
        return 2

    survivors: list[Mutation] = []
    caught: list[Mutation] = []
    uncompilable: list[Mutation] = []

    for i, m in enumerate(MUTATIONS, start=1):
        file_path = REPO_ROOT / m.file
        print(f"\n[{i}/{len(MUTATIONS)}] {m.description}", flush=True)
        print(f"  file: {m.file}", flush=True)

        try:
            original = apply_mutation(file_path, m)
        except RuntimeError as e:
            print(f"  SKIP: {e}", flush=True)
            continue

        try:
            ok_build, build_log = run_build(build_dir, m.target)
            if not ok_build:
                print("  uncompilable (mutation did not produce valid C++)",
                      flush=True)
                uncompilable.append(m)
                continue

            ok_tests, test_log = run_tests(build_dir, args.test_filter)
            if ok_tests:
                print("  SURVIVED -- no test caught the mutation", flush=True)
                survivors.append(m)
            else:
                print("  caught", flush=True)
                caught.append(m)
        finally:
            restore(file_path, original)

    # Final report.
    print("\n" + "=" * 60)
    print(f"caught:       {len(caught)}/{len(MUTATIONS)}")
    print(f"survived:     {len(survivors)}/{len(MUTATIONS)}")
    print(f"uncompilable: {len(uncompilable)}/{len(MUTATIONS)}")
    if survivors:
        print("\nSurvivors (no test caught these -- gaps in coverage):")
        for m in survivors:
            print(f"  - {m.description}  [{m.file}]")

    # The build dir has the LAST mutation's object files baked in.  Rebuild
    # all targets we touched so a follow-up `ctest` against this build dir
    # reflects the (now-restored) original source rather than the last
    # mutation.  Without this, the user gets confusing failures from a
    # stale incremental build.
    print("\nrestoring build (rebuilding mutated targets against original source)")
    targets = sorted({m.target for m in MUTATIONS})
    for t in targets:
        run_build(build_dir, t)

    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
