#!/usr/bin/env python

import os

env = SConscript("../godot-cpp/SConstruct")
env["ENV"]["TERM"] = os.environ["TERM"]  # clang colors

# Using this option makes a warning. Too bad!
opts = Variables([], ARGUMENTS)

opts.Add(BoolVariable("generate_luau_bindings",
         "Force generation of Luau bindings.", False))
opts.Add(BoolVariable("tests", "Build tests", False))
opts.Add(BoolVariable("iwyu", "Run include-what-you-use on main source", False))

opts.Update(env)

env_base = env.Clone()
env_base["CPPDEFINES"] = []  # irrelevant to externs

Export("env")
Export("env_base")
SConscript("extern/SCSub_Luau.py")
SConscript("SCSub_bindgen.py")

env_main = env.Clone()

# We do not want to export any symbols we don't need to.
# Strictly speaking, only the init function must be exported.
if not env_main.get("is_msvc", False):
    env_main.Append(CXXFLAGS=["-fvisibility=hidden"])

if env["iwyu"]:
    env_main["CC"] = "include-what-you-use"
    env_main["CXX"] = "include-what-you-use"

sources = Glob("src/*.cpp", exclude=["src/register_types.cpp"])

env_main.Append(CPPPATH=["src/"])

# Catch2
if env["tests"]:
    env_main.Append(CPPPATH=["extern/Catch2/extras/"])
    sources.append(File("extern/Catch2/extras/catch_amalgamated.cpp"))

    sources.append(env_main.SharedObject("src/register_types.cpp",
                   CPPDEFINES=env["CPPDEFINES"] + ["TESTS_ENABLED"]))

    sources += Glob("tests/*.cpp")
    env_main.Append(CPPPATH=["tests/"])
else:
    sources.append(File("src/register_types.cpp"))

if env["platform"] == "macos":
    library = env_main.SharedLibrary(
        "../../bin/luau-script/libluau-script.{}.{}.framework/libluau-script.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env_main.SharedLibrary(
        "../../bin/luau-script/libluau-script{}{}".format(
            env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
