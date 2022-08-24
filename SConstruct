#!/usr/bin/env python

import os
from luau_binding_generator import scons_emit_files, scons_generate_bindings

env = SConscript("../godot-cpp/SConstruct")
env["ENV"]["TERM"] = os.environ["TERM"]  # clang colors

# Using this option makes a warning. Too bad!
opts = Variables([], ARGUMENTS)
opts.Add(BoolVariable("generate_luau_bindings",
         "Force generation of Luau bindings.", False))

opts.Update(env)

# We do not want to export any symbols we don't need to.
# Strictly speaking, only the init function must be exported.
if not env.get("is_msvc", False):
    env.Append(CXXFLAGS=["-fvisibility=hidden"])

env.Append(BUILDERS={"GenerateLuauBindings": Builder(
    action=scons_generate_bindings, emitter=scons_emit_files)})

luau_bindings = env.GenerateLuauBindings(
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

luau_dir = "extern/luau/"
luau_includes = [
    "Common",
    "Ast",
    "Compiler",
    "VM",
]

env.Append(CPPPATH=[
    "src/",
    "gen/include/"
] + [luau_dir + subdir + "/include/" for subdir in luau_includes])

sources = Glob("src/*.cpp")
for subdir in luau_includes:
    sources += Glob("{}{}/src/*.cpp".format(luau_dir, subdir))

sources.extend([f for f in luau_bindings if str(f).endswith(".cpp")])

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "../../bin/luau-script/libluau-script.{}.{}.framework/libluau-script.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "../../bin/luau-script/libluau-script.{}.{}.{}{}".format(
            env["platform"], env["target"], env["arch_suffix"], env["SHLIBSUFFIX"]
        ),
        source=sources,
    )

Default(library)
