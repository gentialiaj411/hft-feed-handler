#!/usr/bin/env bash
# Local TSAN smoke harness mirroring the CI tsan job.
# Configures + builds (if BUILD_DIR is empty), then runs the
# concurrency-relevant test targets under TSAN.
# Usage:
#   bash scripts/local_run_tsan.sh                       # default build dir ~/mdh-tsan-smoke
#   BUILD_DIR=~/mdh-tsan-smoke bash scripts/local_run_tsan.sh
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-$HOME/mdh-tsan-smoke}"
SRC_DIR="${SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"

export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:abort_on_error=1"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
    -DMF_ENABLE_FUZZ=OFF \
    -DMF_BUILD_AF_XDP=OFF \
    -DMF_BUILD_PHASE_B=OFF \
    -DMF_BUILD_BENCH_SWEEP=OFF
fi

cmake --build "${BUILD_DIR}" -j --target \
  test_core_spmc_seqlock_ring \
  test_phase3_feature_bridge \
  test_phase2_jit_bridge \
  test_runtime_shard_router \
  test_runtime_sharded_pipeline_determinism \
  test_phase_d_spsc_external_storage

PATTERNS=(
  '^core_spmc_seqlock_ring$'
  '^phase3_feature_bridge$'
  '^phase2_jit_bridge$'
  '^runtime_'
  '^phase_d_spsc_external_storage$'
)

fail=0
for P in "${PATTERNS[@]}"; do
  echo "=== Pattern: ${P} ==="
  if ! ctest --test-dir "${BUILD_DIR}" --output-on-failure -R "${P}"; then
    fail=1
  fi
done

if [[ "${fail}" -ne 0 ]]; then
  echo "Some TSAN tests failed." >&2
  exit 1
fi

echo "All TSAN tests passed."
