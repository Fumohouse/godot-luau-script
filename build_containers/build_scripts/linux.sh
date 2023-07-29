#!/usr/bin/env bash

cp -r files/godot-luau-script godot-luau-script
cd godot-luau-script

scons_opts_base="platform=linux arch=x86_64 use_llvm=yes"

scons target=editor $scons_opts_base
scons target=template_release $scons_opts_base

zip -r /root/out/linux_x64 bin
