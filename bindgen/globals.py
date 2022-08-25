from . import constants, utils, common
from .utils import append


def generate_luau_globals(src_dir, api):
    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_bindings.h"

#include <lua.h>

#include "luagd.h"

void luaGD_openglobals(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")
""")

    indent_level = 1

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
