#!/usr/bin/env bash
set -euo pipefail

echo "Pinning methodology (example):"
echo "  sudo cpupower frequency-set -g performance"
echo "  taskset -c 2 ./build/phase1_parser_validate <ITCH_FILE>"