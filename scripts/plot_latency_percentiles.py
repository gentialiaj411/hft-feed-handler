"""Render a latency-percentile bar chart from feed_hot_path / phase3_feature_latency artifacts.

Reads two JSON artifacts that contain per-run p50/p99/p999, plots them side-by-side
showing both the stage breakdown and the across-run stability.
"""
from __future__ import annotations

import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "docs" / "images" / "latency_percentiles.png"

HOT_PATH = REPO / "bench" / "results" / "feed_hot_path_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json"
FEAT_PATH = REPO / "bench" / "results" / "feature_latency_wsl2_4b5ab45af37c4ea1fee77aea27e89ef8fa35ff5a.json"


def load_runs(path: Path) -> list[dict]:
    with path.open() as fh:
        data = json.load(fh)
    return data["runs"]


def main() -> None:
    hot_runs = load_runs(HOT_PATH)
    feat_runs = load_runs(FEAT_PATH)

    percentiles = ["p50_ns", "p99_ns", "p999_ns"]
    pct_labels = ["p50", "p99", "p99.9"]

    fig, ax = plt.subplots(figsize=(9, 4.5), dpi=140)
    x = np.arange(len(percentiles))
    width = 0.18
    palette = {
        "feed_hot_path/run1": ("#1f77b4", -1.5),
        "feed_hot_path/run2": ("#4a90d9", -0.5),
        "phase3_feature_latency/run1": ("#d62728", 0.5),
        "phase3_feature_latency/run2": ("#e8625f", 1.5),
    }
    for label, runs in (("feed_hot_path", hot_runs), ("phase3_feature_latency", feat_runs)):
        for run in runs:
            key = f"{label}/run{run['run']}"
            color, offset = palette[key]
            values = [run[p] for p in percentiles]
            ax.bar(x + offset * width, values, width, color=color, label=key, edgecolor="black", linewidth=0.4)
            for xi, val in zip(x + offset * width, values):
                ax.annotate(f"{val} ns", (xi, val), ha="center", va="bottom", fontsize=7)

    ax.set_xticks(x)
    ax.set_xticklabels(pct_labels)
    ax.set_yscale("log")
    ax.set_ylabel("latency (ns, log scale)")
    ax.set_title("Pipeline latency percentiles — 2M synthetic events, WSL2 (taskset -c 2)")
    ax.grid(axis="y", which="both", linestyle=":", alpha=0.4)
    ax.legend(loc="upper left", fontsize=8, ncols=2)
    ax.set_axisbelow(True)

    fig.tight_layout()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
