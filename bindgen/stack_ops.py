from . import constants
from . import utils
from .utils import should_skip_class, write_file, append

binding_generator = utils.load_cpp_binding_generator()


def generate_stack_ops(src_dir, include_dir, api):
    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append("""\
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#include <lualib.h>
#include <cmath>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/classes/object.hpp>
""")

    header.append("""\
#pragma once

#include "luagd_stack.h"

#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>
""")

    # Object includes
    skip_classes = ["Object", "ClassDB"]
    classes = [g_class for g_class in api["classes"]
               if not (g_class["name"] in skip_classes)]

    for g_class in classes:
        snake_name = binding_generator.camel_to_snake(g_class["name"])
        include = f"#include <godot_cpp/classes/{snake_name}.hpp>"
        src.append(include)
        header.append(include)

    src.append("")
    header.append("\nusing namespace godot;\n")

    # Builtin classes
    builtins_filtered = [
        b_class for b_class in api["builtin_classes"] if not should_skip_class(b_class["name"])]

    for b_class in builtins_filtered:
        class_name = b_class["name"]
        metatable_name = constants.builtin_metatable_prefix + class_name

        if "has_destructor" in b_class and b_class["has_destructor"]:
            src.append(
                f"LUA_UDATA_STACK_OP({class_name}, \"{metatable_name}\", DTOR({class_name}));")
        else:
            src.append(
                f"LUA_UDATA_STACK_OP({class_name}, \"{metatable_name}\", NO_DTOR);")

        header.append(f"template class LuaStackOp<{class_name}>;")

    src.append("")
    header.append("")

    # Object classes
    for g_class in classes:
        class_name = g_class["name"]
        metatable_name = constants.class_metatable_prefix + class_name

        src.append(f"LUA_OBJECT_STACK_OP({class_name});")
        header.append(f"template class LuaStackOp<{class_name} *>;")

    src.append("")
    header.append("")

    # Variant

    indent_level = 0

    # push
    src.append("""\
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"

template <>
void LuaStackOp<Variant>::push(lua_State *L, const Variant &value) {
    switch (value.get_type()) {
        case Variant::NIL:
            lua_pushnil(L);
            return;

        case Variant::BOOL:
            lua_pushboolean(L, value.operator bool());
            return;

        case Variant::INT:
            lua_pushinteger(L, value.operator int());
            return;

        case Variant::FLOAT:
            lua_pushnumber(L, value.operator float());
            return;

        case Variant::STRING:
            LuaStackOp<String>::push(L, value.operator String());
            return;

        case Variant::OBJECT:
            LuaStackOp<Object *>::push(L, value.operator Object *());
            return;
""")

    indent_level += 2

    for b_class in builtins_filtered:
        class_name = b_class["name"]
        class_snake = binding_generator.camel_to_snake(class_name).upper()

        operator_name = class_name

        # Fixes VSCode errors with certain operators
        if class_name == "RID" or class_name == "AABB":
            operator_name = "godot::" + class_name

        append(src, indent_level, f"""\
case Variant::{class_snake}:
    LuaStackOp<{class_name}>::push(L, value.operator {operator_name}());
    return;\
""")

        if b_class != builtins_filtered[-1]:
            src.append("")

    indent_level -= 2

    src.append("""\
    }
}

#pragma GCC diagnostic pop
""")

    # get
    src.append("""\
template <>
Variant LuaStackOp<Variant>::get(lua_State *L, int index) {
    if (lua_isnil(L, index))
        return Variant();

    if (lua_isboolean(L, index))
        return Variant(static_cast<bool>(lua_toboolean(L, index)));

    // TODO: this is rather frail...
    if (lua_isnumber(L, index)) {
        double value = lua_tonumber(L, index);
        double int_part;

        if (std::modf(value, &int_part) == 0.0)
            return Variant(lua_tointeger(L, index));
        else
            return Variant(value);
    }

    if (lua_isstring(L, index))
        return Variant(LuaStackOp<String>::get(L, index));

    if (LuaStackOp<Object *>::is(L, index))
        return Variant(LuaStackOp<Object *>::get(L, index));
""")

    indent_level += 1

    for b_class in builtins_filtered:
        class_name = b_class["name"]

        append(src, indent_level, f"""\
if (LuaStackOp<{class_name}>::is(L, index))
    return Variant(LuaStackOp<{class_name}>::get(L, index));
""")

    indent_level -= 1
    src.append("""\
    luaL_error(L, "stack position %d: expected Variant-compatible type, got %s", index, lua_typename(L, lua_type(L, index)));
}\
""")

    # is
    # TODO: What types are listed here should probably change (e.g. Callable support, if table arg support is added for lists/dicts)
    src.append("""
template <>
bool LuaStackOp<Variant>::is(lua_State *L, int index) {
    if (lua_istable(L, index) ||
            lua_isfunction(L, index) ||
            lua_islightuserdata(L, index) ||
            lua_isthread(L, index))
        return false;

    if (lua_type(L, index) == LUA_TUSERDATA) {
        if (LuaStackOp<Object *>::is(L, index))
            return true;
""")

    indent_level += 2

    for b_class in builtins_filtered:
        class_name = b_class["name"]

        append(src, indent_level, f"""\
if (LuaStackOp<{class_name}>::is(L, index))
    return true;
""")

    indent_level -= 2
    src.append("""\
        return false;
    }

    return true;
}
""")

    # check
    src.append("""\
template <>
Variant LuaStackOp<Variant>::check(lua_State *L, int index) {
    return LuaStackOp<Variant>::get(L, index);
}
""")

    header.append("\ntemplate class LuaStackOp<Variant>;")

    # Save
    write_file(src_dir / "luagd_bindings_stack.gen.cpp", src)
    write_file(include_dir / "luagd_bindings_stack.gen.h", header)
