#!/usr/bin/env bash

cp -r files/godot-luau-script godot-luau-script
cd godot-luau-script

scons_opts_base="platform=macos arch=universal osxcross_sdk=darwin22.4"

export SCONS_CACHE=/root/build/macos
scons target=editor $scons_opts_base
scons target=template_release $scons_opts_base

zip -r /root/out/macos_universal bin
