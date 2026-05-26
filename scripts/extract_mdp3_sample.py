#!/usr/bin/env python3
"""Extract the public CME MDP3 cert incremental feed sample into bench/data/."""
from __future__ import annotations

import hashlib
import sys
import urllib.request
import zipfile
from pathlib import Path

SAMPLE_URL = (
    "https://github.com/kolybelkin/java-cme-mdp3-handler/raw/master/"
    "mbp-only/src/cucumber/sim/data/incr/311_AX_224.0.31.2_17511.zip"
)
OUT_NAME = "mdp3_cert_incr_311_AX_17511.bin"


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    out_dir = root / "bench" / "data"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / OUT_NAME
    zip_path = out_dir / "mdp3_cert_incr_311_AX_17511.zip"

    print(f"fetching {SAMPLE_URL}")
    urllib.request.urlretrieve(SAMPLE_URL, zip_path)

    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        if len(names) != 1:
            print(f"unexpected zip entries: {names}", file=sys.stderr)
            return 1
        data = zf.read(names[0])

    out_path.write_bytes(data)
    sha = hashlib.sha256(data).hexdigest().upper()
    print(f"wrote {out_path} bytes={len(data)} sha256={sha}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
