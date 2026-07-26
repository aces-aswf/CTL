#!/usr/bin/env bash
# Build the project under a sanitizer and run the test suite.  Used to
# catch the class of bugs unit tests can't see: heap leaks, use-after-
# free, double-free (ASan), data races (TSan), undefined behaviour (UBSan).
#
# Usage:
#   tools/run_sanitizer_tests.sh address    # AddressSanitizer
#   tools/run_sanitizer_tests.sh thread     # ThreadSanitizer
#   tools/run_sanitizer_tests.sh undefined  # UndefinedBehaviorSanitizer
#
# Exits 0 if every test passes under the sanitizer, non-zero otherwise.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <address|thread|undefined>" >&2
    exit 2
fi

case "$1" in
    address)   san_kind=ADDRESS  ;;
    thread)    san_kind=THREAD   ;;
    undefined) san_kind=UNDEFINED ;;
    *) echo "unknown sanitizer: $1 (use address, thread, or undefined)" >&2; exit 2 ;;
esac

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir="/tmp/build-cpu-perf-san-${1}"

echo "==> configuring build in ${build_dir}"
cmake -B "${build_dir}" -S "${repo_root}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCTL_SANITIZER="${san_kind}" \
    -DCTL_BUILD_TOOLS=ON \
    -DCTL_BUILD_TESTS=ON

echo "==> building"
cmake --build "${build_dir}" -j

echo "==> running tests under ${san_kind}Sanitizer"
# halt_on_error=0 lets every assertion run; otherwise the first failure
# masks every other potential issue in the same binary.
case "${san_kind}" in
    ADDRESS)
        # Apple's ASan does not support leak detection (only available
        # in LSan-enabled platforms, which on Apple Silicon means none).
        # Don't request it -- ASan aborts at startup if asked.
        if [[ "$(uname -s)" == "Darwin" ]]; then
            san_opts="halt_on_error=0"
        else
            san_opts="halt_on_error=0:detect_leaks=1"
        fi
        ;;
    THREAD)    san_opts="halt_on_error=0:history_size=7"  ;;
    UNDEFINED) san_opts="print_stacktrace=1:halt_on_error=0" ;;
esac

ASAN_OPTIONS="${san_opts}" \
TSAN_OPTIONS="${san_opts}" \
UBSAN_OPTIONS="${san_opts}" \
ctest --test-dir "${build_dir}" --output-on-failure
