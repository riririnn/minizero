#!/bin/bash
set -e

IMAGE_NAME="${IMAGE_NAME:-minizero-gpu-full}"

echo "Running MiniZero with GPU using Docker image: ${IMAGE_NAME}"

docker run --gpus all \
  --cap-add=SYS_PTRACE \
  --security-opt seccomp=unconfined \
  --network=host \
  --ipc=host \
  --rm -it \
  -w /workspace \
  -v "$(pwd):/workspace" \
  -e container=docker \
  "${IMAGE_NAME}" \
  bash -lc 'git config --global --add safe.directory /workspace && exec bash'
