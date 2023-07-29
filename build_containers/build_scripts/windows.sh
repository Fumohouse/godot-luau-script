#!/usr/bin/env bash

cp -r files/godot-luau-script godot-luau-script
cd godot-luau-script

scons_opts_base="platform=windows arch=x86_64"

export SCONS_CACHE=/root/build/windows
scons target=editor $scons_opts_base
scons target=template_release $scons_opts_base

zip -r /root/out/windows_x64 bin
