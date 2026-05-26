#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/bpf/afxdp_redirect.bpf.c"
OUT="${ROOT}/build/afxdp_redirect.bpf.o"

HDR_GEN="$(ls -d /usr/src/linux-headers-*-generic 2>/dev/null | head -1 || true)"
HDR_MAIN="$(ls -d /usr/src/linux-headers-[0-9]* 2>/dev/null | grep -v generic | head -1 || true)"

if [[ -z "${HDR_GEN}" || -z "${HDR_MAIN}" ]]; then
  echo "linux-headers (generic + main) required for BPF compile" >&2
  exit 1
fi

mkdir -p "${ROOT}/build"

clang -g -O2 -target bpf -nostdinc -D__TARGET_ARCH_x86 \
  -I"${HDR_GEN}/arch/x86/include/generated/uapi" \
  -I"${HDR_GEN}/arch/x86/include/generated" \
  -I"${HDR_MAIN}/include/uapi" \
  -I"${HDR_MAIN}/arch/x86/include/uapi" \
  -I/usr/include \
  -c "${SRC}" -o "${OUT}"

echo "wrote ${OUT}"
