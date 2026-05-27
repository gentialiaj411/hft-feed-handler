#!/usr/bin/env bash
# Generate Python gRPC stubs for proto/tick_service.proto into python/gen/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/python/gen"
mkdir -p "${OUT}"
python3 -m grpc_tools.protoc \
  -I "${ROOT}/proto" \
  --python_out="${OUT}" \
  --grpc_python_out="${OUT}" \
  "${ROOT}/proto/tick_service.proto"

# Make gen a package.
touch "${OUT}/__init__.py"
echo "Generated ${OUT}/tick_service_pb2.py and tick_service_pb2_grpc.py"
