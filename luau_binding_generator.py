# Mirrors a lot of what the godot-cpp binder does.
# Some code is similar or the same.

import json
from pathlib import Path

from bindgen.stack_ops import generate_stack_ops
from bindgen.builtins import generate_luau_builtins
from bindgen.classes import generate_luau_classes, get_luau_class_sources
from bindgen.ptrcall import generate_ptrcall


def open_api(filepath):
    api = None

    with open(filepath) as api_file:
        api = json.load(api_file)

    return api


def scons_emit_files(target, source, env):
    output_dir = target[0].abspath
    api = open_api(str(source[0]))

    src_dir = Path(output_dir) / "gen" / "src"

    files = [
        # Builtins
        env.File("gen/src/luagd_builtins.gen.cpp"),

        # Classes
        env.File("gen/include/luagd_classes.gen.h"),
        env.File("gen/src/luagd_classes.gen.cpp"),

        # Stack
        env.File("gen/include/luagd_bindings_stack.gen.h"),
        env.File("gen/src/luagd_bindings_stack.gen.cpp"),

        # Ptrcall
        env.File("gen/include/luagd_ptrcall.gen.h"),
        env.File("gen/src/luagd_ptrcall.gen.cpp"),
    ] + get_luau_class_sources(src_dir, api, env)

    env.Clean(files, target)

    return target + files, source


def scons_generate_bindings(target, source, env):
    output_dir = target[0].abspath
    api = open_api(str(source[0]))

    # Initialize output folders
    src_dir = Path(output_dir) / "gen" / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    include_dir = Path(output_dir) / "gen" / "include"
    include_dir.mkdir(parents=True, exist_ok=True)

    # Codegen
    generate_stack_ops(src_dir, include_dir, api)
    generate_ptrcall(src_dir, include_dir)

    generate_luau_builtins(src_dir, api)
    generate_luau_classes(src_dir, include_dir, api)

    return None
