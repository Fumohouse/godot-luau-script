#!/usr/bin/env bash

source ./config.sh
echo "Building godot-luau-script (image tag: $tag)"

# Checkout
git clone $remote ./files/godot-luau-script || /usr/bin/env true
pushd ./files/godot-luau-script
git clean -dfX
git pull
git checkout $rev
git submodule update --init --recursive
popd

# Build
podman_run() {
    podman run -it --rm -v ./files:/root/files:z,ro -v ./out:/root/out:z \
        -v "./build_scripts/$1.sh":/root/build.sh:ro \
        -v ./build:/root/build:z \
        "gls-$1":$tag ./build.sh
}

for arg in "$@"
do
    case $arg in
        "linux") podman_run linux ;;
        "windows") podman_run windows ;;
        "macos") podman_run macos ;;
    esac
done
