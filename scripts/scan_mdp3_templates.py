#!/usr/bin/env python3
"""Scan CME cert .bin sample for SBE template IDs."""
from __future__ import annotations

import struct
import sys
from collections import Counter
from pathlib import Path

INIT_OFFSET = 10


def iter_packets(data: bytes):
    off = INIT_OFFSET
    while off + 10 <= len(data):
        off += 8
        if off + 2 > len(data):
            break
        block_size = struct.unpack_from(">H", data, off)[0]
        off += 2
        if block_size == 0:
            continue
        if off + block_size > len(data):
            break
        yield data[off : off + block_size]
        off += block_size


def template_ids(payload: bytes) -> list[int]:
    out: list[int] = []
    if len(payload) < 22:
        return out
    pos = 12  # after 12-byte MDP packet header
    while pos + 10 <= len(payload):
        msg_size = struct.unpack_from("<H", payload, pos)[0]
        if msg_size < 10 or pos + msg_size > len(payload):
            break
        template_id = struct.unpack_from("<H", payload, pos + 4)[0]
        out.append(template_id)
        pos += msg_size
    return out


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("bench/data/mdp3_cert_incr_311_AX_17511.bin")
    data = path.read_bytes()
    counts: Counter[int] = Counter()
    packets = 0
    for pkt in iter_packets(data):
        packets += 1
        for tid in template_ids(pkt):
            counts[tid] += 1
        if packets >= 5000:
            break
    print(f"packets_scanned={packets}")
    for tid, n in counts.most_common(30):
        print(f"template_{tid}={n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
