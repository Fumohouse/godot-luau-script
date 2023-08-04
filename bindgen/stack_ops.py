from . import constants
from .utils import should_skip_class, write_file


def generate_stack_ops(src_dir, include_dir, api):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/variant.hpp>
""")

    header.append("""\
#pragma once

#include "luagd_stack.h"

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

        if class_name in ["Array", "Dictionary", "StringName", "NodePath", "String"]:
            # Special cases
            continue
        elif class_name.endswith("Array"):
            array_elem_types = {
                "PackedByteArray": ("uint32_t", "INT"),
                "PackedInt32Array": ("int32_t", "INT"),
                "PackedInt64Array": ("int64_t", "INT"),
                "PackedFloat32Array": ("float", "FLOAT"),
                "PackedFloat64Array": ("double", "FLOAT"),
                "PackedStringArray": ("String", "STRING"),
                "PackedVector2Array": ("Vector2", "VECTOR2"),
                "PackedVector3Array": ("Vector3", "VECTOR3"),
                "PackedColorArray": ("Color", "COLOR"),
            }

            array_elem_type, array_elem_variant_type = array_elem_types[class_name]
            array_elem_variant_type = "Variant::" + array_elem_variant_type

            src.append(f"ARRAY_STACK_OP_IMPL({class_name}, {array_elem_variant_type}, {array_elem_type}, \"{metatable_name}\")")
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
    write_file(src_dir / "luagd_bindings_stack.gen.cpp", src)
    write_file(include_dir / "luagd_bindings_stack.gen.h", header)
