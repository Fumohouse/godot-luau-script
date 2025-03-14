# Mirrors a lot of what the godot-cpp binder does.
# Some code is similar or the same.

import json
from pathlib import Path

from bindgen.stack_ops import generate_stack_ops
from bindgen.api_bin import generate_api_bin
from bindgen.typedefs import generate_typedefs

import platform

major, minor, patch = platform.python_version_tuple()

if int(major) > 3 or (int(major) == 3 and int(minor) >= 11):
    import tomllib
else:
    import tomli as tomllib


def scons_emit_files(target, source, env):
    files = [
        # Stack
        env.File("gen/include/core/builtins_stack.gen.inc"),
        env.File("gen/src/builtins_stack.gen.cpp"),

        # Extension API
        env.File("gen/src/extension_api_bin.gen.cpp")
    ]

    env.Clean(files, target)

    return target + files, source


def scons_generate_bindings(target, source, env):
    output_dir = target[0].abspath

    # Open API
    api = None
    with open(str(source[0])) as api_file:
        api = json.load(api_file)

    # Open class settings
    class_settings = {}
    with open(str(source[1]), "rb") as settings_file:
        class_settings = tomllib.load(settings_file)

    # Initialize output folders
    src_dir = Path(output_dir) / "gen" / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    include_dir = Path(output_dir) / "gen" / "include"
    include_dir.mkdir(parents=True, exist_ok=True)

    defs_dir = Path(output_dir) / "definitions"

    # Codegen
    generate_stack_ops(src_dir, include_dir, api)
    generate_api_bin(src_dir, api, class_settings)
    generate_typedefs(defs_dir, api, str(source[2]), str(source[3]))

    return None
