#!/usr/bin/env bash
set -euo pipefail

EVENTS="${1:-2000000}"
GAP_WINDOW="${2:-256}"
CAPACITY="${3:-1048576}"
OUT_DIR="${4:-artifacts/perf}"

mkdir -p "${OUT_DIR}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

OUT_FILE="${OUT_DIR}/phase2_pipeline_bench_$(date +%Y%m%d_%H%M%S).txt"

{
  echo "host=$(uname -a)"
  echo "events=${EVENTS}"
  echo "gap_window=${GAP_WINDOW}"
  echo "capacity=${CAPACITY}"
  taskset -c 2 ./build/phase2_pipeline_bench \
    --events "${EVENTS}" \
    --gap-window "${GAP_WINDOW}" \
    --capacity "${CAPACITY}"
} | tee "${OUT_FILE}"

echo "saved=${OUT_FILE}"
