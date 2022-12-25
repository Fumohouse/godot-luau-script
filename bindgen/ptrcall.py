from . import utils, constants, common
from .utils import write_file

binding_generator = utils.load_cpp_binding_generator()

# ! godot-cpp is_pod_type
conv_types = [
    # "Nil",
    # "void",
    "bool",
    # "real_t",
    "float",
    # "double",
    # "int",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    # "int64_t",
    "uint32_t",
    # "uint64_t",
]


def use_ptr(class_name, builtin_classes):
    return not utils.should_skip_class(class_name) and (True in [class_name == b_class["name"] for b_class in builtin_classes])


def generate_arg(arg_name, arg_type, index, api):
    builtin_classes = api["builtin_classes"]
    correct_type = common.get_luau_type(arg_type, api)

    if correct_type.endswith("*"):
        decl = f"void *{arg_name};"
        call = f"LuaPtrcallArg<{correct_type}>::get_obj(L, {index}, &{arg_name})"

        return decl, call, arg_name
    elif use_ptr(correct_type, builtin_classes):
        decl = f"{correct_type} *{arg_name};"
        call = f"LuaPtrcallArg<{correct_type} *>::get(L, {index}, &{arg_name})"

        return decl, call, arg_name
    else:
        enc_type = binding_generator.get_gdextension_type(correct_type)

        decl = f"{enc_type} {arg_name};"
        call = f"LuaPtrcallArg<{correct_type}>::get(L, {index}, &{arg_name})"
        name = f"&{arg_name}"

        return decl, call, name


def generate_arg_required(arg_name, arg_type, index, api):
    builtin_classes = api["builtin_classes"]
    correct_type = common.get_luau_type(arg_type, api)

    call = f"LuaPtrcallArg<{correct_type}>::check(L, {index})"

    if correct_type.endswith("*"):
        decl = f"void *{arg_name} = LuaPtrcallArg<{correct_type}>::check_obj(L, {index});"
        varcall = f"Variant((Object *){arg_name})"

        return decl, arg_name, varcall
    elif use_ptr(correct_type, builtin_classes):
        decl = f"{correct_type} *{arg_name} = LuaPtrcallArg<{correct_type} *>::check(L, {index});"
        varcall = f"Variant(*{arg_name})"

        return decl, arg_name, varcall
    else:
        enc_type = binding_generator.get_gdextension_type(correct_type)

        decl = f"{enc_type} {arg_name} = {call};"
        name = f"&{arg_name}"
        varcall = f"Variant({arg_name})"

        return decl, name, varcall


def generate_ptrcall(src_dir, include_dir):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "luagd_ptrcall.gen.h"

#include <lua.h>
#include <godot_cpp/core/method_ptrcall.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"
""")

    header.append("""\
#pragma once

#include <lua.h>
#include <godot_cpp/classes/object.hpp>

#include "luagd_ptrcall.h"
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"
""")

    # ! godot-cpp get_encoded_arg

    # Types which require PtrToArg conversion
    for c_type in conv_types:
        enc_type = binding_generator.get_gdextension_type(c_type)

        src.append(f"""\
bool LuaPtrcallArg<{c_type}>::get(lua_State *L, int index, {enc_type} *ptr)
{{
    if (!LuaStackOp<{c_type}>::is(L, index))
        return false;

    PtrToArg<{c_type}>::encode(LuaStackOp<{c_type}>::get(L, index), ptr);

    return true;
}}

{enc_type} LuaPtrcallArg<{c_type}>::check(lua_State *L, int index)
{{
    {enc_type} encoded;
    PtrToArg<{c_type}>::encode(LuaStackOp<{c_type}>::check(L, index), &encoded);

    return encoded;
}}
""")

        header.append(f"""\
template <>
class LuaPtrcallArg<{c_type}>
{{
public:
    static bool get(lua_State *L, int index, {enc_type} *ptr);
    static {enc_type} check(lua_State *L, int index);
}};
""")

    # Save
    write_file(src_dir / "luagd_ptrcall.gen.cpp", src)
    write_file(include_dir / "luagd_ptrcall.gen.h", header)
