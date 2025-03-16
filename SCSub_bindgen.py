#!/usr/bin/env python

import os
import bindgen.luau_api_generator as luau_api_generator
import bindgen.luau_binding_generator as luau_binding_generator

Import("env")

env_gen = env.Clone()

env_gen.Append(
    BUILDERS={
        "GenerateLuauApi": Builder(
            action=luau_api_generator.generate_bindings,
            emitter=luau_api_generator.emit_files,
        ),
        "GenerateLuauBindings": Builder(
            action=luau_binding_generator.generate_bindings,
            emitter=luau_binding_generator.emit_files,
        ),
    }
)

luau_api = env_gen.GenerateLuauApi()
luau_bindings = env_gen.GenerateLuauBindings()

if env["generate_luau_bindings"]:
    NoCache(luau_api)
    NoCache(luau_bindings)
    AlwaysBuild(luau_api)
    AlwaysBuild(luau_bindings)

sources = [f for f in luau_bindings if str(f).endswith(".cpp")]

env.Append(CPPPATH=["gen/include/"])
env_gen.Append(CPPPATH=["src/", "gen/include/"])

lib = env_gen.Library("gdluau_gen", source=sources)

# Prepend important! This library depends on godot-cpp, so it should be linked *after* this.
env.Prepend(LIBS=[lib])
