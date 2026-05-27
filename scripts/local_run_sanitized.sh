#!/usr/bin/env bash
# Local ASAN+UBSAN smoke harness mirroring the CI asan-ubsan job.
# Configures + builds (if BUILD_DIR is empty) and runs the same labels CI runs.
# Usage:
#   bash scripts/local_run_sanitized.sh                  # default build dir ~/mdh-asan-smoke
#   BUILD_DIR=~/mdh-asan-smoke bash scripts/local_run_sanitized.sh
#   MF_RUN_HEAVY_REGRESSION=1 bash scripts/local_run_sanitized.sh   # opt into 41 GB ITCH journal
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-$HOME/mdh-asan-smoke}"
SRC_DIR="${SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:abort_on_error=1"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DMF_ENABLE_FUZZ=OFF \
    -DMF_BUILD_AF_XDP=OFF \
    -DMF_BUILD_PHASE_B=OFF \
    -DMF_BUILD_BENCH_SWEEP=OFF
fi

cmake --build "${BUILD_DIR}" -j

LABELS=(parser core phase2 phase3 phase4 phase_c research runtime)

fail=0
for L in "${LABELS[@]}"; do
  echo "=== Label: ${L} ==="
  if ! ctest --test-dir "${BUILD_DIR}" --output-on-failure -L "${L}"; then
    fail=1
  fi
done

if [[ "${fail}" -ne 0 ]]; then
  echo "Some sanitized tests failed." >&2
  exit 1
fi

echo "All sanitized tests passed."
