from . import utils
from .utils import append
from . import constants

def generate_luau_classes(src_dir, api):
    classes = api["classes"]
    singletons = api["singletons"]

    src = [constants.header_comment, ""]

    src.append("""\
#include "luagd_bindings.h"

#include <lua.h>
#include <lualib.h>

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

        # Constructors
        ctor_name = "luaGD_class_no_ctor"
        if g_class["is_instantiable"] and not class_name == "ClassDB":
            ctor_name = "luaGD_class_ctor"

        append(src, indent_level, f"""\
// Constructor
lua_pushstring(L, "{class_name}");
lua_pushcclosure(L, {ctor_name}, "{class_name}.__call", 1);
lua_setfield(L, -2, "__call");
""")

        # Singleton getter
        singleton_matches = [s for s in singletons if s["type"] == class_name]
        if len(singleton_matches) > 0:
            singleton = singleton_matches[0]

            append(src, indent_level, f"""\
// Singleton getter
lua_pushcfunction(L, [](lua_State *L) -> int
{{
    static GDNativeObjectPtr singleton_obj = internal::gdn_interface->global_get_singleton("{singleton["name"]}");
    static GDObjectInstanceID singleton_id = internal::gdn_interface->object_get_instance_id(singleton_obj);

    if (lua_gettop(L) > 0)
        luaL_error(L, "singleton getter takes no arguments");

    LuaStackOp<Object *>::push(L, ObjectDB::get_instance(singleton_id));
    return 1;
}}, "{class_name}.GetSingleton");

lua_setfield(L, -3, "GetSingleton");
""")

        # Set magic flag, readonlies & clean up
        append(src, indent_level, "luaGD_poplib(L, true);")

        indent_level -= 1
        append(src, indent_level, "} // " + class_name)

        if g_class != classes[-1]:
            src.append("")

    src.append("}")

    # Save
    utils.write_file(src_dir / "luagd_classes.gen.cpp", src)