from . import constants
from .utils import should_skip_class, write_file


def generate_stack_ops(src_dir, include_dir, api):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "core/stack.h"

#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/variant.hpp>
""")

    header.append("""\
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;
""")

    # Builtin classes
    builtins_filtered = [
        b_class for b_class in api["builtin_classes"] if not should_skip_class(b_class["name"])]

    for b_class in builtins_filtered:
        class_name = b_class["name"]
        metatable_name = constants.builtin_metatable_prefix + class_name

        if class_name in ["StringName", "NodePath", "String"]:
            # Special cases
            continue
        elif "has_destructor" in b_class and b_class["has_destructor"]:
            src.append(
                f"UDATA_STACK_OP_IMPL({class_name}, \"{metatable_name}\", DTOR({class_name}));")
        else:
            src.append(
                f"UDATA_STACK_OP_IMPL({class_name}, \"{metatable_name}\", NO_DTOR);")

        header.append(f"STACK_OP_PTR_DEF({class_name})")

    src.append("")
    header.append("")

    # Save
    core_include_dir = include_dir / "core"
    core_include_dir.mkdir(parents=True, exist_ok=True)

    write_file(src_dir / "builtins_stack.gen.cpp", src)
    write_file(core_include_dir / "builtins_stack.gen.inc", header)
