#!/usr/bin/env python

import os

env = Environment(tools=["default"], PLATFORM="")
env["disable_exceptions"] = False

# clang terminal colors
env["ENV"]["TERM"] = os.environ.get("TERM")

Export("env")
SConscript("extern/godot-cpp/SConstruct")

if env["platform"] == "macos":
    env.Append(RANLIBFLGS="-no_warning_for_no_symbols")

# Using this option makes a warning. Too bad!
opts = Variables([], ARGUMENTS)

opts.Add(BoolVariable("generate_luau_bindings",
         "Force generation of Luau bindings", False))
opts.Add(BoolVariable("tests", "Build tests", False))
opts.Add(BoolVariable("iwyu", "Run include-what-you-use on main source", False))

opts.Update(env)

env_base = env.Clone()
env_base["CPPDEFINES"] = []  # irrelevant to externs

Export("env_base")
SConscript("extern/SCSub_Luau.py")
SConscript("SCSub_bindgen.py")

env_main = env.Clone()

if env["iwyu"]:
    env_main["CC"] = "include-what-you-use"
    env_main["CXX"] = "include-what-you-use "

    env_main.Prepend(CXXFLAGS=["-Xiwyu", "--transitive_includes_only"])

sources = Glob("src/*.cpp")
sources += Glob("src/*/*.cpp")

env_main.Append(CPPPATH=["src/"])

# Catch2
if env["tests"]:
    if env["target"] != "editor":
        raise ValueError("Tests can only be enabled on the editor target")

    env_main.Append(CPPDEFINES="TESTS_ENABLED", CPPPATH=["extern/Catch2/extras/"])
    sources.append(File("extern/Catch2/extras/catch_amalgamated.cpp"))

    sources += Glob("tests/*.cpp")
    sources += Glob("tests/*/*.cpp")
    env_main.Append(CPPPATH=["tests/"])

if env["platform"] == "macos":
    library = env_main.SharedLibrary(
        "bin/luau-script/libluau-script.{}.{}.framework/libluau-script.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "bin/luau-script/libluau-script.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "bin/luau-script/libluau-script.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env_main.SharedLibrary(
        "bin/luau-script/libluau-script{}{}".format(
            env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
