#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-artifacts/perf}"
mkdir -p "${OUT_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_FILE="${OUT_DIR}/phase2_tests_${STAMP}.txt"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

{
  echo "host=$(uname -a)"
  echo "timestamp=${STAMP}"
  echo "== phase2 tests =="
  ./build/test_nasdaq_itch_parser
  ./build/test_iex_deep_parser
  ./build/test_phase2_sequence_tracker
  ./build/test_phase2_recovery_merge
  ./build/test_phase2_recovery_simulator
  ./build/test_phase2_determinism_crc
  ./build/test_phase2_pipeline
  ./build/test_phase2_jit_bridge
  echo "phase2_tests=PASS"
} | tee "${OUT_FILE}"

echo "saved=${OUT_FILE}"
