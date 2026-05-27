#!/usr/bin/env bash
# Runs every test label against a build dir and prints a one-line summary per label.
# Usage: BUILD_DIR=~/mdh-gcc bash scripts/local_run_all.sh
set -uo pipefail

BUILD_DIR="${BUILD_DIR:-$HOME/mdh-gcc}"

LABELS=(parser core phase2 phase3 phase4 phase_c research runtime phase_d phase_e)

fail=0
for L in "${LABELS[@]}"; do
  out=$(ctest --test-dir "${BUILD_DIR}" --output-on-failure -L "${L}" 2>&1)
  pass_line=$(echo "${out}" | grep -E 'tests passed|tests failed' | tail -1)
  if echo "${out}" | grep -q 'FAILED'; then
    fail=1
    echo "=== ${L}: FAIL ==="
    echo "${out}" | tail -20
  else
    echo "${L}: ${pass_line}"
  fi
done

if [[ "${fail}" -ne 0 ]]; then
  echo "Some labels failed." >&2
  exit 1
fi
echo "All labels green."
