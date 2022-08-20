from . import utils
from .utils import append
from . import constants

def generate_luau_classes(src_dir, classes):
    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_bindings.h"

#include <lua.h>

#include "luagd.h"

void luaGD_openclasses(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdClassesLoaded");
""")

    indent_level = 1

    for g_class in classes:
        # Names
        class_name = g_class["name"]
        metatable_name = constants.class_metatable_prefix + class_name

        # Class definitions
        append(src, indent_level, f"""\
{{ // {class_name}
    luaGD_newlib(L, "{class_name}", "{metatable_name}");
""")

        indent_level += 1

        # Set magic flag, readonlies & clean up
        append(src, indent_level, "luaGD_poplib(L, true);")

        indent_level -= 1
        append(src, indent_level, "} // " + class_name)

        if g_class != classes[-1]:
            src.append("")

    src.append("}")

    # Save
    utils.write_file(src_dir / "luagd_classes.gen.cpp", src)