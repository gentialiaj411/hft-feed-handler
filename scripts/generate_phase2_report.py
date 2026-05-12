#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


def load_text(path: Path) -> str:
    data = path.read_bytes()
    if b"\x00" in data[:256]:
        try:
            return data.decode("utf-16")
        except Exception:
            pass
    return data.decode("utf-8", errors="replace")


def clean_text(text: str) -> str:
    return text.replace("\x00", "")


def parse_kv_lines(lines: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in lines:
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def split_sections(lines: list[str], header: str) -> list[list[str]]:
    sections: list[list[str]] = []
    i = 0
    while i < len(lines):
        if lines[i].strip() != header:
            i += 1
            continue
        i += 1
        current: list[str] = []
        while i < len(lines):
            s = lines[i].strip()
            if s.startswith("[") and s.endswith("]"):
                break
            current.append(lines[i])
            i += 1
        sections.append(current)
    return sections


def md_row(cols: list[str]) -> str:
    return "| " + " | ".join(cols) + " |"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: generate_phase2_report.py <artifact_file>")
        return 2

    p = Path(sys.argv[1])
    if not p.exists():
        print(f"artifact not found: {p}")
        return 1

    lines = clean_text(load_text(p)).splitlines()
    root = parse_kv_lines(lines)

    ab_sections = split_sections(lines, "[phase2_ab_evidence]")
    ab_runs = [parse_kv_lines(sec) for sec in ab_sections]

    print(f"# Phase 2 Evidence Report")
    print()
    print(f"- Artifact: `{p}`")
    if "timestamp" in root:
      print(f"- Timestamp: `{root['timestamp']}`")
    if "host" in root:
      print(f"- Host: `{root['host']}`")
    print()

    print("## Core Metrics")
    print(md_row(["Metric", "Value"]))
    print(md_row(["---", "---"]))
    for key in [
        "phase2_tests",
        "events_in",
        "events_per_sec",
        "elapsed_ns",
        "accepted",
        "dropped_publish_overflow",
        "dropped_duplicate_or_old",
        "buffered_out_of_order",
        "dropped_gap_too_large",
        "dropped_gap_too_large_pending_evicted",
        "recovery_requests",
        "recovery_reinjected",
        "merged_crc32",
    ]:
        if key in root:
            print(md_row([key, root[key]]))
    print()

    if ab_runs:
        print("## A/B Evidence")
        print(md_row([
            "Mode",
            "CRC Match",
            "Baseline CRC",
            "Raced CRC",
            "Drop A",
            "Drop B",
            "Dropped A",
            "Dropped B",
            "Arb Accepted",
            "Arb Duplicates",
            "Arb GapTooLarge",
            "Pipe GapTooLarge",
            "Pipe RecoveryReq",
        ]))
        print(md_row(["---"] * 13))
        for run in ab_runs:
            mode = "complementary" if run.get("complementary_drops", "false") == "true" else "independent"
            print(md_row([
                mode,
                run.get("crc_match", ""),
                run.get("baseline_crc32", ""),
                run.get("raced_crc32", ""),
                run.get("drop_rate_a", ""),
                run.get("drop_rate_b", ""),
                run.get("dropped_a", ""),
                run.get("dropped_b", ""),
                run.get("arb_accepted", ""),
                run.get("arb_duplicate_or_old", ""),
                run.get("arb_gap_too_large", ""),
                run.get("pipe_dropped_gap_too_large", ""),
                run.get("pipe_recovery_requests", ""),
            ]))
        print()

    print("## Readout")
    if any(run.get("crc_match") == "true" and run.get("complementary_drops") == "true" for run in ab_runs):
        print("- Complementary A/B run preserved baseline CRC.")
    if any(run.get("crc_match") == "false" and run.get("complementary_drops") != "true" for run in ab_runs):
        print("- Independent random drops produced CRC divergence (expected when both feeds miss packets).")
    if "merged_crc32" in root:
        print(f"- Final merged CRC: `{root['merged_crc32']}`")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
