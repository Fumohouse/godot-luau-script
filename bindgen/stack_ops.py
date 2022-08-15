from . import constants
from .utils import should_skip_class, write_file


def generate_stack_ops(src_dir, include_dir, classes):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "luagd_stack.h"
#include "luagd_builtins_stack.gen.h"
""")

    header.append("""\
#pragma once

#include "luagd_stack.h"

#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;
""")

    for b_class in classes:
        class_name = b_class["name"]
        if should_skip_class(class_name):
            continue

        metatable_name = constants.builtin_metatable_prefix + class_name

        if "has_destructor" in b_class and b_class["has_destructor"]:
            src.append(
                f"LUA_UDATA_STACK_OP({class_name}, {metatable_name}, DTOR({class_name}));")
        else:
            src.append(
                f"LUA_UDATA_STACK_OP({class_name}, {metatable_name}, NO_DTOR);")

        header.append(f"template class LuaStackOp<{class_name}>;")

    # Save
    write_file(src_dir / "luagd_builtins_stack.gen.cpp", src)
    write_file(include_dir / "luagd_builtins_stack.gen.h", header)
