from . import constants
from . import utils
from .utils import append

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


def generate_builtin_constructor(class_name, constructors):
    label_format = "gd_builtin_ctor_{}_{}"

    ctor = []
    indent_level = 0

    append(ctor, indent_level, "// Constructor")
    append(ctor, indent_level,
           "lua_pushcfunction(L, [](lua_State *L) -> int\n{")
    indent_level += 1

    # Remove the first argument to call
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
        variant_type = "GDNATIVE_VARIANT_TYPE_" + \
            binding_generator.camel_to_snake(class_name).upper()

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

                encoded_arg, encoded_name = binding_generator.get_encoded_arg(
                    arg_name, argument["type"], argument["meta"] if "meta" in argument else None)

                if len(encoded_arg) > 0:
                    encoded_arg = "\n" + \
                        "\n".join([e[1:] for e in encoded_arg])
                else:
                    encoded_arg = ""

                arg_names.append(encoded_name)

                append(ctor, indent_level, f"""\
if (!LuaStackOp<{arg_type}>::is(L, {idx}))
    {break_line}

{arg_type} {arg_name} = LuaStackOp<{arg_type}>::get(L, {idx});{encoded_arg}
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


def generate_luau_builtins(src_dir, classes):
    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_builtins.h"

#include "luagd.h"
#include "luagd_stack.h"
#include "luagd_builtins_stack.gen.h"

#include <lua.h>
#include <lualib.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/core/method_ptrcall.hpp>
#include <godot_cpp/core/builtin_ptrcall.hpp>

void luaGD_openbuiltins(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded");
    const char *meta_type_key = "__fortype";
""")

    indent_level = 1

    for b_class in classes:
        class_name = b_class["name"]

        if utils.should_skip_class(class_name):
            continue

        append(src, indent_level, "{ // " + class_name)
        indent_level += 1

        metatable_name = constants.builtin_metatable_prefix + class_name
        append(src, indent_level,
               f"const char *metatable_name = \"{metatable_name}\";")

        # Create metatable
        append(src, indent_level, "luaL_newmetatable(L, metatable_name);")

        # TODO

        # Pop metatable
        append(src, indent_level, "lua_pop(L, 1);\n")

        # Create global table
        append(src, indent_level, """\
lua_newtable(L); // global table
lua_createtable(L, 0, 3); // metatable
lua_pushstring(L, metatable_name);
lua_setfield(L, -2, meta_type_key);
""")

        # Constructor
        if "constructors" in b_class:
            append(src, indent_level, generate_builtin_constructor(
                class_name,
                b_class["constructors"]))

            src.append("")
            append(src, indent_level, "lua_setfield(L, -2, \"__call\");\n")

        # TODO: methods on global table

        append(src, indent_level, f"""\
lua_setreadonly(L, -1, true);
lua_setmetatable(L, -2);
lua_setglobal(L, \"{class_name}\");\
""")

        indent_level -= 1
        append(src, indent_level, "} // " + class_name)

        if b_class != classes[-1]:
            src.append("")

    src.append("}\n")

    # Save
    utils.write_file(src_dir / "luagd_builtins.gen.cpp", src)
