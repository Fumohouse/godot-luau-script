#!/usr/bin/env python

env = SConscript("../godot-cpp/SConstruct")

luau_dir = "extern/luau/"
luau_includes = [
    "Common",
    "Ast",
    "Compiler",
    "VM",
]

env.Append(CPPPATH=["src/"])
env.Append(CPPPATH=[luau_dir + subdir + "/include/" for subdir in luau_includes])

sources = Glob("src/*.cpp")
for subdir in luau_includes:
    sources += Glob("{}{}/src/*.cpp".format(luau_dir, subdir))

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
