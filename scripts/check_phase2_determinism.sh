#!/usr/bin/env bash
set -euo pipefail

RUNS="${1:-5}"
EVENTS="${2:-200000}"
GAP_WINDOW="${3:-256}"
CAPACITY="${4:-1048576}"
OUT_DIR="${5:-artifacts/perf}"

mkdir -p "${OUT_DIR}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

crc_ref=""
for i in $(seq 1 "${RUNS}"); do
  out_file="${OUT_DIR}/phase2_determinism_run_${i}.txt"
  ./build/phase2_pipeline_bench \
    --events "${EVENTS}" \
    --gap-window "${GAP_WINDOW}" \
    --capacity "${CAPACITY}" | tee "${out_file}"

  crc="$(grep -E '^merged_crc32=' "${out_file}" | tail -n 1 | cut -d'=' -f2)"
  if [[ -z "${crc}" ]]; then
    echo "missing merged_crc32 in run ${i}" >&2
    exit 1
  fi
  if [[ -z "${crc_ref}" ]]; then
    crc_ref="${crc}"
  elif [[ "${crc}" != "${crc_ref}" ]]; then
    echo "determinism check failed: run ${i} crc=${crc} ref=${crc_ref}" >&2
    exit 1
  fi
done

echo "deterministic=true"
echo "runs=${RUNS}"
echo "merged_crc32=${crc_ref}"
