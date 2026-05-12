#!/usr/bin/env bash
set -euo pipefail

EVENTS="${1:-500000}"
GAP_WINDOW="${2:-256}"
CAPACITY="${3:-1048576}"
DROP_A="${4:-0.02}"
DROP_B="${5:-0.02}"
SEED="${6:-11}"
OUT_DIR="${7:-artifacts/perf}"
MODE="${8:-independent}"

mkdir -p "${OUT_DIR}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_FILE="${OUT_DIR}/phase2_ab_evidence_${STAMP}.txt"
REPORT_FILE="${OUT_DIR}/phase2_ab_report_${STAMP}.md"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

EXTRA=()
if [[ "${MODE}" == "complementary" ]]; then
  EXTRA+=(--complementary-drops)
fi

./build/phase2_ab_evidence \
  --events "${EVENTS}" \
  --gap-window "${GAP_WINDOW}" \
  --capacity "${CAPACITY}" \
  --drop-a "${DROP_A}" \
  --drop-b "${DROP_B}" \
  --seed "${SEED}" \
  "${EXTRA[@]}" | tee "${OUT_FILE}"

python3 scripts/generate_phase2_report.py "${OUT_FILE}" > "${REPORT_FILE}"

echo "saved=${OUT_FILE}"
echo "saved=${REPORT_FILE}"
