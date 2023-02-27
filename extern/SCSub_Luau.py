#!/usr/bin/env python

Import("env")
Import("env_base")

env_luau = env_base.Clone()
env_luau.Append(CPPDEFINES="LUA_USE_LONGJMP=1")

luau_dir = "#extern/luau/"
luau_includes = [
    "Common",
    "Ast",
    "Compiler",
    "VM",
    "CodeGen",
]

sources = []
for subdir in luau_includes:
    sources += Glob(f"{luau_dir}{subdir}/src/*.cpp")

includes = [luau_dir + subdir + "/include/" for subdir in luau_includes]
env.Append(CPPPATH=includes)
env_luau.Append(CPPPATH=includes)
env_luau.Append(CPPPATH=[luau_dir + subdir + "/src/" for subdir in luau_includes])

lib = env_luau.Library("luau", source=sources)
env.Append(LIBS=[lib])
