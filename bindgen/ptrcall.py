from . import utils
from .utils import write_file
from . import constants

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


def generate_arg(arg_name, arg_type, index, builtin_classes):
    correct_type = binding_generator.correct_type(arg_type)
    call = f"LuaPtrcallArg<{correct_type}>::get(L, {index}, &{arg_name})"

    if correct_type.endswith("*"):
        decl = f"void *{arg_name};"

        return (decl, call, arg_name)
    elif not utils.should_skip_class(correct_type) and (True in [correct_type == b_class["name"] for b_class in builtin_classes]):
        decl = f"{correct_type} *{arg_name};"
        call = f"LuaPtrcallArg<{correct_type} *>::get(L, {index}, &{arg_name})"

        return (decl, call, arg_name)
    else:
        enc_type = binding_generator.get_gdnative_type(correct_type)

        decl = f"{enc_type} {arg_name};"
        name = f"&{arg_name}"

        return (decl, call, name)


def generate_ptrcall(src_dir, include_dir):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "luagd_ptrcall.gen.h"

#include <lua.h>
#include <godot_cpp/core/method_ptrcall.hpp>

#include "luagd_stack.h"
#include "luagd_builtins_stack.gen.h"
""")

    header.append("""\
#pragma once

#include <lua.h>
#include <godot_cpp/classes/object.hpp>

#include "luagd_stack.h"
#include "luagd_builtins_stack.gen.h"

template <typename T>
class LuaPtrcallArg
{
public:
    static bool get(lua_State *L, int index, T *ptr)
    {
        if (!LuaStackOp<T>::is(L, index))
            return false;

        *ptr = LuaStackOp<T>::get(L, index);

        return true;
    }
};

template <typename T>
class LuaPtrcallArg<T *>
{
public:
    // Variant types
    static bool get(lua_State *L, int index, T **ptr)
    {
        if (!LuaStackOp<T>::is(L, index))
            return false;

        *ptr = LuaStackOp<T>::get_ptr(L, index);

        return true;
    }

    // Objects
    static bool get(lua_State *L, int index, void **ptr)
    {
        if (!LuaStackOp<Object *>::is(L, index))
            return false;

        Object *obj = LuaStackOp<Object *>::get(L, index);
        *ptr = obj->_owner;

        return true;
    }
};
""")

    # ! godot-cpp get_encoded_arg

    # Types which require PtrToArg conversion
    for c_type in conv_types:
        enc_type = binding_generator.get_gdnative_type(c_type)

        src.append(f"""\
bool LuaPtrcallArg<{c_type}>::get(lua_State *L, int index, {enc_type} *ptr)
{{
    if (!LuaStackOp<{c_type}>::is(L, index))
        return false;

    PtrToArg<{c_type}>::encode(LuaStackOp<{c_type}>::get(L, index), ptr);

    return true;
}}
""")

        header.append(f"""\
template <>
class LuaPtrcallArg<{c_type}>
{{
public:
    static bool get(lua_State *L, int index, {enc_type} *ptr);
}};
""")

    # Save
    write_file(src_dir / "luagd_ptrcall.gen.cpp", src)
    write_file(include_dir / "luagd_ptrcall.gen.h", header)
