#!/usr/bin/env bash
# One-shot diagnostic: prints summary stats over the equity_delta and trade
# rows of bench/results/ofi_backtest_raw.csv. Used to sanity-check the
# bootstrap p-value rerun after fixing block_bootstrap_mean_pvalue.
set -euo pipefail

CSV="${1:-bench/results/ofi_backtest_raw.csv}"

for series in equity_delta trade bucket; do
  echo "=== series=${series} ==="
  awk -F, -v s="${series}" 'NR>1 && $1==s {
    n++; sum+=$3
    if ($3<=0) le++; else gt++
  } END {
    if (n==0) { print "no rows"; exit }
    printf "n=%d sum=%g mean=%g le_zero=%d gt_zero=%d\n", n, sum, sum/n, le, gt
  }' "${CSV}"
done
