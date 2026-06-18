#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD="${BUILD_DIR:-build-wsl}"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" -L alpha_lab --output-on-failure
ctest --test-dir "$BUILD" -L alpha_lab_baseline --output-on-failure
"$BUILD/alpha_lab" --journal bench/data/itch_20190130_5m_for_ofi.journal --out bench/results/alpha_lab
echo "alpha_lab artifacts in bench/results/alpha_lab/"
