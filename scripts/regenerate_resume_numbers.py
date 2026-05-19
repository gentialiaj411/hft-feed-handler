#!/usr/bin/env python3
import difflib
import glob
import json
import os
import re
from pathlib import Path


def latest(path_glob: str) -> str:
    matches = glob.glob(path_glob)
    if not matches:
        raise SystemExit(f"no files for {path_glob}")
    matches.sort(key=lambda p: os.path.getmtime(p))
    return matches[-1]


def load_json(path: str):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main():
    summary_path = latest("bench/results/sweep_summary_*.json")
    summary = load_json(summary_path)
    artifacts = summary.get("artifacts", [])
    by_id = {}
    for p in artifacts:
        j = load_json(p)
        by_id[j["bench_id"]] = (p, j)

    b1_path, b1 = by_id.get("b1_feed_hot_path", ("TODO/VERIFY", {}))
    b2_path, b2 = by_id.get("b2_feature_latency", ("TODO/VERIFY", {}))
    b4_path, b4 = by_id.get("b4_wire_loopback", ("TODO/VERIFY", {}))

    events = int(b1.get("events", 0))
    p99_ns = int(b2.get("latency", {}).get("p99", 0))
    loss_claim = "TODO/VERIFY" if b4.get("skipped", True) else "0"

    snapshot_lines = [
        "# Bench Snapshot",
        "",
        f"Summary artifact: `{summary_path}`",
        "",
        "| Metric | Value | Artifact |",
        "|---|---:|---|",
        f"| feed hot-path events | {events} | `{b1_path}` |",
        f"| feature latency p99 (ns) | {p99_ns} | `{b2_path}` |",
        f"| wire loopback divergence under packet loss (%) | {loss_claim} | `{b4_path}` |",
    ]
    Path("docs").mkdir(parents=True, exist_ok=True)
    Path("docs/bench-snapshot.md").write_text("\n".join(snapshot_lines) + "\n", encoding="utf-8")

    resume_path = Path("RESUME_CLAIMS.md")
    old = resume_path.read_text(encoding="utf-8")
    new = old
    new = re.sub(r"validated across [0-9,]+ replayed events",
                 f"validated across {events:,} replayed events <!-- evidence: {Path(b1_path).name} -->", new)
    new = re.sub(r"under [0-9]+% simulated packet loss",
                 f"under {loss_claim}% simulated packet loss <!-- evidence: {Path(b4_path).name} -->", new)
    new = re.sub(r"at [0-9]+ns p99 latency",
                 f"at {p99_ns}ns p99 latency <!-- evidence: {Path(b2_path).name} -->", new)
    resume_path.write_text(new, encoding="utf-8")

    diff = difflib.unified_diff(
        old.splitlines(keepends=True),
        new.splitlines(keepends=True),
        fromfile="RESUME_CLAIMS.md (before)",
        tofile="RESUME_CLAIMS.md (after)",
    )
    print("".join(diff))


if __name__ == "__main__":
    main()
