#!/usr/bin/env bash
set -euo pipefail

# Creates an isolated network namespace with a veth pair for AF_XDP loopback ingest.
# Requires: root (sudo), clang, ip, libbpf headers for userspace build.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NETNS="${MF_AF_XDP_NETNS:-mdh_afxdp}"
VETH_HOST="${MF_AF_XDP_VETH_HOST:-veth0}"
VETH_NS="${MF_AF_XDP_VETH_NS:-veth1}"
HOST_IP="${MF_AF_XDP_HOST_IP:-10.200.1.1/24}"
NS_IP="${MF_AF_XDP_NS_IP:-10.200.1.2/24}"
BPF_SRC="${ROOT}/bpf/afxdp_redirect.bpf.c"
BPF_OUT="${ROOT}/build/afxdp_redirect.bpf.o"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run as root: sudo $0" >&2
  exit 1
fi

if ! command -v clang >/dev/null 2>&1; then
  echo "clang required to compile BPF object" >&2
  exit 1
fi

mkdir -p "${ROOT}/build"
bash "${ROOT}/scripts/compile_afxdp_bpf.sh"

ip netns del "${NETNS}" 2>/dev/null || true
ip netns add "${NETNS}"
ip link add "${VETH_HOST}" type veth peer name "${VETH_NS}"
ip link set "${VETH_NS}" netns "${NETNS}"

ip addr add "${HOST_IP}" dev "${VETH_HOST}"
ip link set "${VETH_HOST}" up

ip netns exec "${NETNS}" ip addr add "${NS_IP}" dev "${VETH_NS}"
ip netns exec "${NETNS}" ip link set lo up
ip netns exec "${NETNS}" ip link set "${VETH_NS}" up

# AF_XDP consumer attaches BPF via libbpf at runtime (see AfxdpReceiver::open).

echo "netns=${NETNS} consumer_if=${VETH_NS} dst_ip=${NS_IP%%/*} bpf_obj=${BPF_OUT}"
echo "Export for tests/bench:"
echo "  export MF_AF_XDP_IFNAME=${VETH_NS}"
echo "  export MF_AF_XDP_BPF_OBJ=${BPF_OUT}"
echo "  export MF_AF_XDP_DST_IP=${NS_IP%%/*}"
echo "Run consumer inside netns:"
echo "  ip netns exec ${NETNS} ./build/afxdp_itch_consumer --ifname ${VETH_NS} --bpf-obj ${BPF_OUT} --out /tmp/afxdp.journal"
echo "Run producer from host:"
echo "  ./build/afxdp_itch_producer --in <itch> --dst-ip ${NS_IP%%/*} --dst-port 5000"
