from . import constants, utils, common
from .utils import append

binding_generator = utils.load_cpp_binding_generator()


def generate_luau_globals(src_dir, api):
    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_bindings.h"

#include <lua.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/godot.hpp>

#include "luagd_builtins.h"

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#include "luagd_ptrcall.h"
#include "luagd_ptrcall.gen.h"

void luaGD_openglobals(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")
""")

    indent_level = 1

    # Utility functions
    utils_to_bind = {
        # math functions not provided by Luau
        "ease": None,
        "lerpf": "lerp",
        "cubic_interpolate": None,
        "bezier_interpolate": None,
        "lerp_angle": None,
        "inverse_lerp": None,
        "range_lerp": None,
        "smoothstep": None,
        "move_toward": None,
        "linear2db": None,
        "db2linear": None,
        "wrapf": "wrap",
        "pingpong": None,

        # other
        "print": None,
        "print_rich": None,
        "printerr": None,
        "printraw": None,
        "print_verbose": None,
        "push_warning": "warn",
        "hash": None,
        "is_instance_valid": None,
    }

    append(src, indent_level, "// Utility functions")

    for util_func in api["utility_functions"]:
        func_name = util_func["name"]
        func_hash = util_func["hash"]

        if not (func_name in utils_to_bind):
            continue

        func_global_name = utils_to_bind[func_name] if utils_to_bind[func_name] else func_name

        append(src, indent_level, f"""\
{{ // {func_name}
    lua_pushcfunction(L, [](lua_State *L) -> int
    {{
        static GDNativePtrUtilityFunction __func = internal::gdn_interface->variant_get_ptr_utility_function("{func_name}", {func_hash});
""")

        indent_level += 2

        # Get arguments
        args_src, _ = common.generate_method_args(None, util_func, api)
        append(src, indent_level, args_src)

        # Call
        # ! For now, none of the utility functions bound return objects. So, the case is not handled.
        if "return_type" in util_func:
            correct_return = common.get_luau_type(
                util_func["return_type"], api)

            append(src, indent_level, f"""\
{binding_generator.get_gdnative_type(correct_return)} ret;
__func(&ret, args.ptr(), args.size());

LuaStackOp<{correct_return}>::push(L, ret);
return 1;\
""")
        else:
            append(src, indent_level, """\
__func(nullptr, args.ptr(), args.size());

return 0;\
""")

        indent_level -= 2
        append(src, indent_level, f"""\
    }}, "Godot.UtilityFunctions.{func_global_name}");

    lua_setglobal(L, "{func_global_name}");
}} // {func_name}
""")

    # Global enums
    enums = api["global_enums"]
    append(src, indent_level, f"""\
// Enums
lua_createtable(L, 0, {len(enums)});
""")

    append(src, indent_level, common.generate_enums(enums, -2))
    append(src, indent_level, "lua_setglobal(L, \"Enum\");")

    src.append("}")

    # Save
    utils.write_file(src_dir / "luagd_globals.gen.cpp", src)
