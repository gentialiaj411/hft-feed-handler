"""Render an equity curve and per-bucket PnL distribution for the OFI backtest.

Reads `bench/results/ofi_backtest_raw.csv`, which has columns (series, period_index, pnl).
`series == 'bucket'` rows are 1-second buckets used for Sharpe; `series == 'trade'` rows
are per-fill PnLs. The plot shows cumulative bucket PnL (the equity curve) and the
distribution of non-zero buckets, communicating the negative-Sharpe story visually.
"""
from __future__ import annotations

import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

REPO = Path(__file__).resolve().parent.parent
CSV_PATH = REPO / "bench" / "results" / "ofi_backtest_raw.csv"
MD_PATH = REPO / "bench" / "results" / "ofi_backtest.md"
OUT = REPO / "docs" / "images" / "ofi_equity_curve.png"


def load_buckets(path: Path) -> np.ndarray:
    values: list[float] = []
    with path.open(newline="") as fh:
        reader = csv.reader(fh)
        next(reader)
        for row in reader:
            if not row or row[0] != "bucket" or len(row) < 3:
                continue
            try:
                values.append(float(row[2]))
            except ValueError:
                continue
    return np.asarray(values, dtype=np.float64)


def parse_metric(md_text: str, key: str) -> str:
    m = re.search(rf"\|\s*{re.escape(key)}\s*\|\s*([^|]+?)\s*\|", md_text)
    return m.group(1).strip() if m else "n/a"


def main() -> None:
    buckets = load_buckets(CSV_PATH)
    equity = np.cumsum(buckets)
    nonzero = buckets[buckets != 0.0]

    sharpe = "n/a"
    p_value = "n/a"
    if MD_PATH.exists():
        md_text = MD_PATH.read_text()
        try:
            sharpe = f"{float(parse_metric(md_text, 'sharpe_annualized')):.3f}"
        except ValueError:
            sharpe = parse_metric(md_text, 'sharpe_annualized')
        try:
            p_value = f"{float(parse_metric(md_text, 'block_bootstrap_p_value')):.2f}"
        except ValueError:
            p_value = parse_metric(md_text, 'block_bootstrap_p_value')

    fig, (ax_eq, ax_hist) = plt.subplots(1, 2, figsize=(11, 4.2), dpi=140, gridspec_kw={"width_ratios": [2, 1]})

    ax_eq.plot(np.arange(equity.size), equity, color="#1f3a5f", linewidth=1.0)
    ax_eq.axhline(0, color="#888", linewidth=0.6, linestyle="--")
    ax_eq.set_xlabel("1-second bucket index (held-out 30% window)")
    ax_eq.set_ylabel("cumulative PnL (price ticks × shares)")
    ax_eq.set_title(f"OFI strategy equity curve — Sharpe {sharpe}, bootstrap p = {p_value}")
    ax_eq.grid(linestyle=":", alpha=0.4)
    ax_eq.set_axisbelow(True)

    if equity.size:
        final = equity[-1]
        ax_eq.text(
            0.02, 0.94,
            f"final cum-PnL = {final:,.0f}  •  non-zero buckets = {nonzero.size} / {buckets.size:,}",
            transform=ax_eq.transAxes,
            fontsize=8,
            bbox={"facecolor": "white", "edgecolor": "#bbb", "boxstyle": "round,pad=0.3"},
        )
        ax_eq.margins(x=0.02, y=0.08)

    if nonzero.size:
        ax_hist.hist(nonzero, bins=40, color="#d62728", edgecolor="black", linewidth=0.4, alpha=0.8)
        ax_hist.axvline(0, color="#888", linewidth=0.6, linestyle="--")
        ax_hist.set_xlabel("per-bucket PnL (non-zero buckets)")
        ax_hist.set_ylabel("count")
        ax_hist.set_title(f"distribution: n = {nonzero.size}")
        ax_hist.grid(linestyle=":", alpha=0.4)
        ax_hist.set_axisbelow(True)
    else:
        ax_hist.text(0.5, 0.5, "no non-zero buckets", ha="center", va="center", transform=ax_hist.transAxes)

    fig.tight_layout()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT)
    print(f"wrote {OUT}")
    print(f"buckets={buckets.size}  non_zero={nonzero.size}  final_pnl={equity[-1] if equity.size else 0:.2f}")


if __name__ == "__main__":
    main()
