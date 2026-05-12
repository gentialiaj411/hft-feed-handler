#!/usr/bin/env bash
set -euo pipefail

NASDAQ_RAW="${1:-data/raw/expanded/nasdaq.bin}"
IEX_PCAP="${2:-data/raw/expanded/iex.bin}"
CBOE_FILE="${3:-}"
OUT_DIR="${4:-artifacts/perf}"

mkdir -p "${OUT_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_FILE="${OUT_DIR}/phase2_evidence_${STAMP}.txt"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

{
  echo "host=$(uname -a)"
  echo "timestamp=${STAMP}"
  echo "nasdaq_raw=${NASDAQ_RAW}"
  echo "iex_pcap=${IEX_PCAP}"
  echo "cboe_file=${CBOE_FILE:-<none>}"
  echo
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
  echo
  echo "== nasdaq raw itch =="
  ./build/phase1_parser_validate --nasdaq-raw-itch "${NASDAQ_RAW}"
  echo
  echo "== phase2 merge jit =="
  if [[ -n "${CBOE_FILE}" ]]; then
    ./build/phase1_parser_validate --phase2-merge-jit "${NASDAQ_RAW}" "${IEX_PCAP}" "${CBOE_FILE}"
  else
    ./build/phase1_parser_validate --phase2-merge-jit "${NASDAQ_RAW}" "${IEX_PCAP}"
  fi
  echo
  echo "== phase2 bench =="
  ./build/phase2_pipeline_bench --events 2000000 --gap-window 256 --capacity 1048576
} | tee "${OUT_FILE}"

echo "saved=${OUT_FILE}"
