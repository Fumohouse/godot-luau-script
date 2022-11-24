#!/usr/bin/env python

import os
from luau_binding_generator import scons_emit_files, scons_generate_bindings

Import("env")

env_gen = env.Clone()

env_gen.Append(BUILDERS={"GenerateLuauBindings": Builder(
    action=scons_generate_bindings, emitter=scons_emit_files)})

luau_bindings = env_gen.GenerateLuauBindings(
    Dir("."),
    [
        os.path.join(
            os.getcwd(), "extern/godot-cpp/godot-headers/extension_api.json"),
        os.path.join(
            os.getcwd(), "extern/godot-cpp/godot-headers/godot/gdnative_interface.h")
    ]
)

if env["generate_luau_bindings"]:
    AlwaysBuild(luau_bindings)

sources = [f for f in luau_bindings if str(f).endswith(".cpp")]

env.Append(CPPPATH=["gen/include/"])
env_gen.Append(CPPPATH=["src/", "gen/include/"])

lib = env_gen.Library("gdluau_gen", source=sources)

# Prepend important! This library depends on godot-cpp, so it should be linked *after* this.
env.Prepend(LIBS=[lib])
