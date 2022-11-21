from . import constants, utils, common
from .utils import append
from .ptrcall import generate_arg, generate_arg_required

binding_generator = utils.load_cpp_binding_generator()


def generate_ctor_help_literal(class_name, constructors):
    lines = []

    for constructor in constructors:
        args_str = ""

        if "arguments" in constructor:
            arguments = constructor["arguments"]

            for argument in arguments:
                args_str += argument["name"] + ": " + argument["type"]

                if argument != arguments[-1]:
                    args_str += ", "

        lines.append(f"- {class_name}({args_str})")

    return "\n".join([constants.indent + f"\"{line}\\n\"" for line in lines])


def generate_builtin_constructor(class_name, variant_type, constructors, api):
    label_format = "gd_builtin_ctor_{}_{}"

    ctor = []
    indent_level = 0

    append(ctor, indent_level, "// Constructor")
    append(ctor, indent_level,
           "lua_pushcfunction(L, [](lua_State *L) -> int\n{")
    indent_level += 1

    # Remove the first argument to __call (the global table)
    append(ctor, indent_level, "lua_remove(L, 1);")

    append(ctor, indent_level, "int argc = lua_gettop(L);\n")

    # No constructors have defaults or varargs. Please let it stay that way
    for constructor in constructors:
        index = constructor["index"]
        expected_arg_count = len(
            constructor["arguments"]) if "arguments" in constructor else 0

        # Nasty goto stuff
        if index != 0:
            append(ctor, indent_level, label_format.format(
                class_name, index) + ":")

        break_line = "goto " + label_format.format(class_name, index + 1) + ";"

        append(ctor, indent_level, "{")
        indent_level += 1

        # Get constructor info
        append(ctor, indent_level,
               "static GDNativePtrConstructor __constructor = " +
               f"internal::gdn_interface->variant_get_ptr_constructor({variant_type}, {index});\n")

        # Check arg count
        append(ctor, indent_level, f"""\
if (argc != {expected_arg_count})
    {break_line}
""")

        # Get arguments
        arg_names = []

        if expected_arg_count > 0:
            for idx, argument in enumerate(constructor["arguments"], 1):
                arg_type = binding_generator.correct_type(argument["type"])
                arg_name = "p_" + argument["name"]

                decl, call, name = generate_arg(
                    arg_name, arg_type, idx, api)
                arg_names.append(name)

                append(ctor, indent_level, f"""\
{decl}
if (!{call})
    {break_line}
""")

        # Allocate userdata & call constructor
        args_str = ""

        if len(arg_names) > 0:
            args_str = ", " + ", ".join(arg_names)

        append(ctor, indent_level,
               f"internal::_call_builtin_constructor(__constructor, LuaStackOp<{class_name}>::alloc(L){args_str});")

        append(ctor, indent_level, "return 1;")

        indent_level -= 1
        append(ctor, indent_level, "}\n")

    # Error and help string
    append(ctor, indent_level, label_format.format(
        class_name, len(constructors)) + ":")

    args_str = generate_ctor_help_literal(class_name, constructors)

    append(ctor, indent_level, f"""\
luaL_error(
    L,
    "no constructors matched. expected one of the following:\\n"
{args_str}
);\
""")

    indent_level -= 1
    append(ctor, indent_level, f"}}, \"{class_name}.ctor\");")

    return "\n".join(ctor)


def generate_method(class_name, variant_type, method, api):
    method_name = method["name"]
    method_hash = method["hash"]
    is_static = method["is_static"]
    is_vararg = method["is_vararg"]

    src = []

    map_name = "__static_funcs" if is_static else "__methods"

    src.append(f"""\
{map_name}["{utils.snake_to_pascal(method_name)}"] = [](lua_State *L) -> int
{{\
""")

    indent_level = 1

    # Find method
    if not is_vararg:
        append(src, indent_level, f"""\
StringName __name = "{method_name}";
static GDNativePtrBuiltInMethod __method = internal::gdn_interface->variant_get_ptr_builtin_method({variant_type}, &__name, {method_hash});\
""")

    # Pull arguments
    args_src, self_name = common.generate_method_args(class_name, method, api)
    append(src, indent_level, args_src)

    # Call
    return_type = None
    ret_ptr_name = "nullptr"

    if "return_type" in method:
        return_type = method["return_type"]
        return_type = common.get_luau_type(return_type, api)

        if not is_vararg:
            append(src, indent_level, f"{return_type} ret;")
            ret_ptr_name = "&ret"

    if is_vararg:
        append(src, indent_level,
               f"""\
static StringName __method_name = "{method_name}";

Variant ret;\
""")

        if is_static:
            append(src, indent_level,
                   f"internal::gdn_interface->variant_call_static({variant_type}, &__method_name, args.ptr(), args.size(), {ret_ptr_name}, nullptr);")
        else:
            append(src, indent_level, f"""\
Variant v_self = *{self_name};
internal::gdn_interface->variant_call(&v_self, &__method_name, args.ptr(), args.size(), &ret, nullptr);\
""")
    else:
        append(src, indent_level,
               f"__method({self_name}, args.ptr(), {ret_ptr_name}, args.size());")

    if return_type:
        append(src, indent_level, f"""\
LuaStackOp<{return_type}>::push(L, ret);

return 1;\
""")
    else:
        append(src, indent_level, "\nreturn 0;")

    indent_level -= 1
    src.append("};")

    return "\n".join(src)


def op_priority(op):
    if not ("right_type" in op):
        return 0

    right_type = binding_generator.correct_type(op["right_type"])
    if right_type == "Variant":
        return 1

    return 0


def generate_op(class_name, metatable_name, variant_type, operators, api):
    label_format = "gd_builtin_op_{}_{}_{}"

    # since Variant is basically a catch-all type, comparison to Variant should always be last
    # otherwise the output could be unexpected
    ops_sorted = sorted(operators, key=op_priority)

    src = []

    metatable_operators = {
        "==": "eq",
        "<": "lt",
        "<=": "le",
        "+": "add",
        "-": "sub",
        "*": "mul",
        "/": "div",
        "%": "mod",
        "unary-": "unm"
    }

    indent_level = 0

    left_decl, left_name, left_varcall = generate_arg_required(
        "left", class_name, 1, api)

    for gd_op, mt_key in metatable_operators.items():
        if not (True in [op["name"] == gd_op for op in operators]):
            continue

        variant_op_name = "GDNATIVE_VARIANT_OP_" + \
            binding_generator.get_operator_id_name(gd_op).upper()

        append(src, indent_level, f"""\
// __{mt_key}
lua_pushcfunction(L, [](lua_State *L) -> int
{{
    {left_decl}
""")

        indent_level += 1

        op_overload_index = 0

        for operator in ops_sorted:
            if operator["name"] != gd_op:
                continue

            if op_overload_index != 0:
                append(src, indent_level, label_format.format(
                    class_name, mt_key, op_overload_index) + ":")

            break_line = "goto " + \
                label_format.format(class_name, mt_key,
                                    op_overload_index + 1) + ";"

            right_variant_type = "GDNATIVE_VARIANT_TYPE_NIL"
            if "right_type" in operator:
                right_type = operator["right_type"]
                if right_type != "Variant":
                    right_variant_type = "GDNATIVE_VARIANT_TYPE_" + \
                        binding_generator.camel_to_snake(right_type).upper()

            append(src, indent_level, f"""\
{{
    static GDNativePtrOperatorEvaluator __op = internal::gdn_interface->variant_get_ptr_operator_evaluator({variant_op_name}, {variant_type}, {right_variant_type});
""")
            indent_level += 1

            right_ptr_name = "nullptr"

            if "right_type" in operator:
                right_type = operator["right_type"]

                right_decl, right_call, right_name = generate_arg(
                    "right", right_type, 2, api)

                right_ptr_name = right_name

                append(src, indent_level, f"""\
{right_decl}
if (!{right_call})
    {break_line}
""")

            ret_type = binding_generator.correct_type(operator["return_type"])
            ret_type_gdn = binding_generator.get_gdnative_type(ret_type)
            append(src, indent_level, f"""\
LuaStackOp<{ret_type}>::push(L, internal::_call_builtin_operator_ptr<{ret_type_gdn}>(__op, {left_name}, {right_ptr_name}));
return 1;\
""")

            indent_level -= 1
            append(src, indent_level, "}\n")

            op_overload_index += 1

        catchall = f"luaL_error(L, \"{metatable_name}.__{mt_key}: no operators matched\");"
        if gd_op == "==":
            catchall = """\
lua_pushboolean(L, false);
return 1;\
"""

        append(src, indent_level,
               f"{label_format.format(class_name, mt_key, op_overload_index)}:")
        append(src, indent_level, catchall)

        indent_level -= 1
        append(src, indent_level, f"""\
}}, "{metatable_name}.__{mt_key}");

lua_setfield(L, -4, "__{mt_key}");
""")

    # Special case: Array length
    if class_name.endswith("Array"):
        append(src, indent_level, f"""\
// __len
lua_pushcfunction(L, [](lua_State *L) -> int
{{
    {left_decl}

    LuaStackOp<int64_t>::push(L, {left_name}->size());
    return 1;
}}, "{metatable_name}.__len");

lua_setfield(L, -4, "__len");
""")

    return "\n".join(src)


def generate_luau_builtins(src_dir, api):
    builtin_classes = api["builtin_classes"]

    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_bindings.h"

#include "luagd_builtins.h"

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#include "luagd_ptrcall.h"
#include "luagd_ptrcall.gen.h"

#include <lua.h>
#include <lualib.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/core/method_ptrcall.hpp>
#include <godot_cpp/core/builtin_ptrcall.hpp>
#include <godot_cpp/templates/vector.hpp>

void luaGD_openbuiltins(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded")
""")

    indent_level = 1

    for b_class in builtin_classes:
        # Names
        class_name = b_class["name"]
        if utils.should_skip_class(class_name):
            continue

        variant_type = "GDNATIVE_VARIANT_TYPE_" + \
            binding_generator.camel_to_snake(class_name).upper()

        metatable_name = constants.builtin_metatable_prefix + class_name

        # Class definition
        append(src, indent_level, f"""\
{{ // {class_name}
    luaGD_newlib(L, \"{class_name}\", \"{metatable_name}\");
""")

        indent_level += 1

        # Methods - Initialization
        statics_ptr_name = "nullptr"
        methods_ptr_name = "nullptr"

        if "methods" in b_class:
            append(src, indent_level, """\
// Methods
static MethodMap __static_funcs;
static MethodMap __methods;

if (__static_funcs.is_empty() && __methods.is_empty())
{\
""")

            statics_ptr_name = "&__static_funcs"
            methods_ptr_name = "&__methods"

            indent_level += 1

            methods = b_class["methods"]
            for method in methods:
                append(src, indent_level, generate_method(
                    class_name, variant_type, method, api))

                if method != methods[-1]:
                    src.append("")

            indent_level -= 1
            append(src, indent_level, "}\n")

            # __namecall
            append(src, indent_level, f"""\
// __namecall
lua_pushstring(L, "{class_name}");
lua_pushlightuserdata(L, &__methods);
lua_pushcclosure(L, luaGD_builtin_namecall, "{metatable_name}.__namecall", 2);
lua_setfield(L, -4, "__namecall");
""")

        # __index
        has_members = "members" in b_class
        has_indexer = "indexing_return_type" in b_class

        # TODO: Keying is kind of suspicious. is_keyed is set true for most builtin types
        #       yet it seems that setters/getters are registered only for Object/Variant.
        #       Also, key/return type is not indicated anywhere and it isn't really safe to assume Variant.
        #is_keyed = b_class["is_keyed"]

        if has_members or has_indexer:
            decl, arg_name, varcall = generate_arg_required(
                "self", class_name, 1, api)

            append(src, indent_level, f"""\
// __index
lua_pushcfunction(L, [](lua_State *L) -> int
{{
    {decl}
    Variant key = LuaStackOp<Variant>::check(L, 2);\
""")
            indent_level += 1

            # Member access
            if has_members:
                members = b_class["members"]

                append(src, indent_level, """
if (key.get_type() == Variant::Type::STRING)
{
    String key_str = key;
""")
                indent_level += 1

                for member in members:
                    member_name = member["name"]
                    member_key = utils.snake_to_camel(member_name)
                    member_correct_type = binding_generator.correct_type(
                        member["type"])

                    append(src, indent_level, f"""\
if (key_str == "{member_key}")
{{
    StringName __name = "{member_name}";
    static GDNativePtrGetter __getter = internal::gdn_interface->variant_get_ptr_getter({variant_type}, &__name);

    {member_correct_type} ret;
    __getter({arg_name}, &ret);

    LuaStackOp<{member_correct_type}>::push(L, ret);
    return 1;
}}\
""")

                    if member != members[-1]:
                        src.append("")

                indent_level -= 1
                append(src, indent_level, "}")

            # Index access
            if has_indexer:
                indexer_type = binding_generator.correct_type(
                    b_class["indexing_return_type"])

                append(src, indent_level, f"""
if (key.get_type() == Variant::Type::INT)
{{
    static GDNativePtrIndexedGetter __getter = internal::gdn_interface->variant_get_ptr_indexed_getter({variant_type});

    {indexer_type} ret;
    __getter({arg_name}, key.operator int64_t() - 1, &ret);

    LuaStackOp<{indexer_type}>::push(L, ret);
    return 1;
}}\
""")

            indent_level -= 1
            append(src, indent_level, f"""
    luaL_error(L, "%s is not a valid member of {class_name}", key.operator String().utf8().get_data());
}}, "{metatable_name}.__index");

lua_setfield(L, -4, "__index");
""")

        # __newindex
        append(src, indent_level, f"""
// __newindex
lua_pushstring(L, "{class_name}");
lua_pushcclosure(L, luaGD_builtin_newindex, "{metatable_name}.__newindex", 1);
lua_setfield(L, -4, "__newindex");
""")

        # Operators
        if "operators" in b_class:
            append(src, indent_level, generate_op(
                class_name, metatable_name, variant_type, b_class["operators"], api))

        # Constants
        if "constants" in b_class:
            append(src, indent_level, "// Constants")

            b_constants = b_class["constants"]
            for constant in b_constants:
                const_name = constant["name"]
                const_type = binding_generator.correct_type(constant["type"])

                append(
                    src, indent_level, f"LUA_BUILTIN_CONST({variant_type}, {const_name}, {const_type})")

            src.append("")

        # Enums
        if "enums" in b_class:
            append(src, indent_level, "// Enums")
            append(src, indent_level, common.generate_enums(b_class["enums"]))

        # Constructor
        if "constructors" in b_class:
            append(src, indent_level, generate_builtin_constructor(
                class_name,
                variant_type,
                b_class["constructors"],
                api))

            src.append("")
            append(src, indent_level, "lua_setfield(L, -2, \"__call\");\n")

        # Global __index
        append(src, indent_level, f"""\
// Global __index
lua_pushstring(L, "{class_name}");
lua_pushlightuserdata(L, {statics_ptr_name});
lua_pushlightuserdata(L, {methods_ptr_name});
lua_pushcclosure(L, luaGD_builtin_global_index, "{class_name}.__index", 3);
lua_setfield(L, -2, "__index");
""")

        # Readonlies, clean up
        append(src, indent_level, "luaGD_poplib(L, false);")

        indent_level -= 1
        append(src, indent_level, "} // " + class_name)

        if b_class != builtin_classes[-1]:
            src.append("")

    src.append("}")

    # Save
    utils.write_file(src_dir / "luagd_builtins.gen.cpp", src)