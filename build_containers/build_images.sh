#!/usr/bin/env bash

source ./config.sh
echo "Building build container images (tag: $tag)"

podman build -t gls-base:${tag} -f Dockerfile.base

podman_build() {
    podman build \
        --build-arg tag=${tag} \
        -t gls-"$1:${tag}" \
        -f "Dockerfile.$1" \
        -v $(pwd)/files:/root/files:z,ro
}

podman_build linux
podman_build windows
podman_build macos
