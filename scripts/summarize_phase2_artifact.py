#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


KEYS = [
    "events_in",
    "elapsed_ns",
    "events_per_sec",
    "accepted",
    "dropped_publish_overflow",
    "dropped_duplicate_or_old",
    "buffered_out_of_order",
    "dropped_gap_too_large",
    "dropped_gap_too_large_pending_evicted",
    "recovery_requests",
    "recovery_reinjected",
    "merged_crc32",
    "phase2_tests",
    "baseline_crc32",
    "raced_crc32",
    "crc_match",
    "drop_rate_a",
    "drop_rate_b",
    "drop_seed",
    "complementary_drops",
    "dropped_a",
    "dropped_b",
    "arb_accepted",
    "arb_accepted_a",
    "arb_accepted_b",
    "arb_duplicate_or_old",
    "arb_gap_buffered",
    "arb_gap_too_large",
    "pipe_accepted",
    "pipe_dropped_gap_too_large",
    "pipe_recovery_requests",
]


def parse_lines(text: str) -> dict[str, str]:
    text = text.replace("\x00", "")
    out: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k in KEYS:
            out[k] = v
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: summarize_phase2_artifact.py <artifact_file>")
        return 2

    p = Path(sys.argv[1])
    if not p.exists():
        print(f"artifact not found: {p}")
        return 1

    values = parse_lines(p.read_text(encoding="utf-8", errors="replace"))
    print(f"artifact={p}")
    for k in KEYS:
        if k in values:
            print(f"{k}={values[k]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
