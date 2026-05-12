#!/usr/bin/env bash
set -euo pipefail

NASDAQ_RAW="${1:-data/raw/expanded/nasdaq.bin}"
IEX_PCAP="${2:-data/raw/expanded/iex.bin}"
CBOE_FILE="${3:-}"
OUT_DIR="${4:-artifacts/perf}"
AB_DROP_A="${5:-0.02}"
AB_DROP_B="${6:-0.02}"
AB_SEED="${7:-11}"

mkdir -p "${OUT_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_FILE="${OUT_DIR}/phase2_evidence_${STAMP}.txt"
REPORT_FILE="${OUT_DIR}/phase2_report_${STAMP}.md"

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
  ./build/test_phase2_ab_arbiter
  ./build/test_phase3_feature_bridge
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
  echo
  echo "== phase2 ab evidence (independent drops) =="
  ./build/phase2_ab_evidence \
    --events 500000 \
    --gap-window 256 \
    --capacity 1048576 \
    --drop-a "${AB_DROP_A}" \
    --drop-b "${AB_DROP_B}" \
    --seed "${AB_SEED}"
  echo
  echo "== phase2 ab evidence (complementary drops) =="
  ./build/phase2_ab_evidence \
    --events 500000 \
    --gap-window 256 \
    --capacity 1048576 \
    --drop-a "${AB_DROP_A}" \
    --drop-b "${AB_DROP_B}" \
    --seed "${AB_SEED}" \
    --complementary-drops
} | tee "${OUT_FILE}"

python3 scripts/generate_phase2_report.py "${OUT_FILE}" > "${REPORT_FILE}"

echo "saved=${OUT_FILE}"
echo "saved=${REPORT_FILE}"
