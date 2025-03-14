# Mirrors a lot of what the godot-cpp binder does.
# Some code is similar or the same.

import json
from pathlib import Path

from bindgen.stack_ops import generate_stack_ops
from bindgen.api_bin import generate_api_bin
from bindgen.typedefs import generate_typedefs


def emit_files(target, source, env):
    in_files = [
        env.File("gen/luau_api.json"),
        env.File("definitions/luauLibTypes.part.d.lua"),
        env.File("definitions/godotTypes.part.d.lua"),
    ]

    out_files = [
        # Stack
        env.File("gen/include/core/builtins_stack.gen.inc"),
        env.File("gen/src/builtins_stack.gen.cpp"),
        # Extension API
        env.File("gen/src/extension_api_bin.gen.cpp"),
    ]

    env.Clean(out_files, target)

    return target + out_files, source + in_files


def generate_bindings(target, source, env):
    # Open API
    api = None
    with open(str(source[0])) as api_file:
        api = json.load(api_file)

    # Initialize output folders
    src_dir = Path("gen/src")
    src_dir.mkdir(parents=True, exist_ok=True)

    include_dir = Path("gen/include")
    include_dir.mkdir(parents=True, exist_ok=True)

    defs_dir = Path("definitions")

    # Codegen
    generate_stack_ops(src_dir, include_dir, api)
    generate_api_bin(src_dir, api)
    generate_typedefs(defs_dir, api, str(source[1]), str(source[2]))

    return None
