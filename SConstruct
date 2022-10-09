#!/usr/bin/env python

import os
import subprocess
import json
from luau_binding_generator import scons_emit_files, scons_generate_bindings

env = SConscript("../godot-cpp/SConstruct")
env["ENV"]["TERM"] = os.environ["TERM"]  # clang colors

# Using this option makes a warning. Too bad!
opts = Variables([], ARGUMENTS)

opts.Add(BoolVariable("generate_luau_bindings",
         "Force generation of Luau bindings.", False))
opts.Add(BoolVariable("use_sccache",
         "Use sccache distributed compiling (must be on PATH).", False))
opts.Add(BoolVariable("tests", "Build tests", False))

opts.Update(env)

if env["use_sccache"]:
    env.Append(ENV={
        "PATH": os.environ["PATH"]
    })

    # set job count according to server info
    sccache_status = subprocess.check_output(["sccache", "--dist-status"])
    sccache_status = json.loads(sccache_status)

    cpus = sccache_status["SchedulerStatus"][1]["num_cpus"]
    cpus = cpus if cpus <= 4 else cpus - 1

    env.SetOption("num_jobs", cpus)
    print(f"sccache: Now using {cpus} cores.")

    env["CC"] = "sccache " + env["CC"]
    env["CXX"] = "sccache " + env["CXX"]

# We do not want to export any symbols we don't need to.
# Strictly speaking, only the init function must be exported.
if not env.get("is_msvc", False):
    env.Append(CXXFLAGS=["-fvisibility=hidden"])

env_base = env.Clone()
env_base["CPPDEFINES"] = []  # irrelevant to externs

Export("env")
Export("env_base")
SConscript("extern/SCSub_Luau.py")

env_main = env.Clone()

# Luau bindgen
env_main.Append(BUILDERS={"GenerateLuauBindings": Builder(
    action=scons_generate_bindings, emitter=scons_emit_files)})

luau_bindings = env_main.GenerateLuauBindings(
    env.Dir("."),
    [
        os.path.join(
            os.getcwd(), "../godot-cpp/godot-headers/extension_api.json"),
        os.path.join(
            os.getcwd(), "../godot-cpp/godot-headers/godot/gdnative_interface.h")
    ]
)

if env["generate_luau_bindings"]:
    AlwaysBuild(luau_bindings)

env_main.Append(CPPPATH=[
    "src/",
    "gen/include/"
])

sources = Glob("src/*.cpp", exclude=["src/register_types.cpp"])

sources.extend([f for f in luau_bindings if str(f).endswith(".cpp")])

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
