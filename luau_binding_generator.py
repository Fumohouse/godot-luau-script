# Mirrors a lot of what the godot-cpp binder does.
# Some code is similar or the same.

import json
from pathlib import Path

from bindgen.stack_ops import generate_stack_ops
from bindgen.api_bin import generate_api_bin
from bindgen.typedefs import generate_typedefs


def open_api(filepath):
    api = None

    with open(filepath) as api_file:
        api = json.load(api_file)

    return api


def scons_emit_files(target, source, env):
    files = [
        # Stack
        env.File("gen/include/luagd_bindings_stack.gen.h"),
        env.File("gen/src/luagd_bindings_stack.gen.cpp"),

        # Extension API
        env.File("gen/src/extension_api_bin.gen.cpp")
    ]

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

    defs_dir = Path(output_dir) / "definitions"
    defs_dir.mkdir(parents=True, exist_ok=True)

    # Codegen
    generate_stack_ops(src_dir, include_dir, api)
    generate_api_bin(src_dir, api, str(source[1]))
    generate_typedefs(defs_dir, api)

    return None
