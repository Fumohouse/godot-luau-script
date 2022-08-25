from . import utils, constants, common
from .utils import append

binding_generator = utils.load_cpp_binding_generator()


def get_src_path(src_dir, class_name):
    return src_dir / "classes" / f"luagd_class_{binding_generator.camel_to_snake(class_name)}.gen.cpp"


# To save as much time as possible, some classes should be ignored
# (and probably have little to no utility in Luau anyway)
def skip_class(class_name):
    # TODO: VisualScript is getting removed soon.
    return class_name.startswith("VisualScript") or \
        class_name.startswith("VisualShaderNode")


def generate_method(class_name, method, api):
    method_name = method["name"]
    method_hash = method["hash"]
    is_static = method["is_static"]

    src = []

    map_name = "__class_info.static_funcs" if is_static else "__class_info.methods"

    src.append(f"""\
{map_name}["{utils.snake_to_pascal(method_name)}"] = [](lua_State *L) -> int
{{
    static GDNativeMethodBindPtr __method_bind = internal::gdn_interface->classdb_get_method_bind("{class_name}", "{method_name}", {method_hash});
""")

    indent_level = 1

    # Pull arguments
    args_src, self_name = common.generate_method_args(
        class_name, method, api)
    append(src, indent_level, args_src)

    # Call
    # there are way too many ways to call
    if method["is_vararg"]:
        append(src, indent_level, f"""\
Variant ret;
GDNativeCallError error;
internal::gdn_interface->object_method_bind_call(__method_bind, {self_name}, args.ptr(), args.size(), &ret, &error);
""")

        if "return_value" in method:
            append(src, indent_level, """\
LuaStackOp<Variant>::push(L, ret);
return 1;\
""")
        else:
            append(src, indent_level, "return 0;")
    elif "return_value" in method:
        return_type = method["return_value"]["type"]
        is_obj = True in [c["name"] == return_type for c in api["classes"]]

        if is_obj:
            append(src, indent_level, f"""\
LuaStackOp<{return_type} *>::push(L, _call_native_mb_ret_obj_arr<{return_type}>(__method_bind, {self_name}, args.ptr()));
return 1;\
""")
        else:
            correct_ret_type = common.get_luau_type(return_type, api)
            gdn_ret_type = binding_generator.get_gdnative_type(
                correct_ret_type)

            append(src, indent_level, f"""\
{gdn_ret_type} ret;
internal::gdn_interface->object_method_bind_ptrcall(__method_bind, {self_name}, args.ptr(), &ret);
LuaStackOp<{correct_ret_type}>::push(L, ret);

return 1;\
""")
    else:
        append(src, indent_level, f"""\
internal::gdn_interface->object_method_bind_ptrcall(__method_bind, {self_name}, args.ptr(), nullptr);
return 0;\
""")

    indent_level -= 1
    src.append("};")

    return "\n".join(src)


def get_luau_class_sources(src_dir, api, env):
    src_files = []

    for g_class in api["classes"]:
        class_name = g_class["name"]

        if skip_class(class_name):
            continue

        src_files.append(env.File(get_src_path(src_dir, class_name)))

    return src_files


def generate_luau_classes(src_dir, include_dir, api):
    classes = api["classes"]
    singletons = api["singletons"]

    class_dir = src_dir / "classes"
    class_dir.mkdir(parents=True, exist_ok=True)

    classes_filtered = [c for c in classes if not skip_class(c["name"])]

    src = [constants.header_comment, ""]
    header = [constants.header_comment, ""]

    src.append(f"""\
#include "luagd_bindings.h"

#include <lua.h>

#include "luagd.h"

#include "luagd_classes.gen.h"

void luaGD_openclasses(lua_State *L)
{{
    LUAGD_LOAD_GUARD(L, "_gdClassesLoaded");

    static ClassRegistry __classes;
    __classes.resize({len(classes_filtered)});

    static bool __first_init = true;
""")

    header.append("""\
#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/templates/vector.hpp>

#include "luagd_bindings.h"
""")

    indent_level = 1

    for class_idx, g_class in enumerate(classes_filtered):
        # Names
        class_name = g_class["name"]
        metatable_name = constants.class_metatable_prefix + class_name

        open_name = f"luaGD_openclass_{binding_generator.camel_to_snake(class_name)}"

        # Header & main src
        header.append(
            f"void {open_name}(lua_State *L, bool first_init, ClassRegistry *classes);")
        append(src, indent_level, f"{open_name}(L, __first_init, &__classes);")

        class_src = [constants.header_comment, ""]
        class_src.append("""\
#include "luagd_classes.gen.h"

#include <lua.h>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#include "luagd_ptrcall.h"
#include "luagd_ptrcall.gen.h"

#include "luagd_bindings.h"
""")

        c_indent = 0

        # Class definitions
        append(class_src, c_indent, f"""\
void {open_name}(lua_State *L, bool first_init, ClassRegistry *classes)
{{
    luaGD_newlib(L, "{class_name}", "{metatable_name}");
""")

        c_indent += 1

        # Class info initialization
        append(class_src, c_indent, """\
if (first_init)
{
    ClassInfo __class_info;\
""")

        c_indent += 1

        # Parent
        if "inherits" in g_class:
            inherits = g_class["inherits"]
            parent_idx_result = [idx for idx, c in enumerate(classes_filtered) if c["name"] == inherits]
            append(class_src, c_indent, f"__class_info.parent_idx = {parent_idx_result[0]};\n")
        else:
            class_src.append("")

        # Methods
        if "methods" in g_class:
            append(class_src, c_indent, "// Methods")

            methods_filtered = [
                m for m in g_class["methods"] if not m["is_virtual"]]
            for method in methods_filtered:
                append(class_src, c_indent, generate_method(
                    class_name, method, api))

                class_src.append("")

        # Properties
        if "properties" in g_class:
            properties = g_class["properties"]

            for prop in properties:
                prop_name = utils.snake_to_camel(prop["name"])
                getter_name = utils.snake_to_pascal(prop["getter"])
                setter_name = utils.snake_to_pascal(prop["setter"])

                append(class_src, c_indent, f"""\
{{
    LuauProperty __property_info;
    __property_info.getter_name = "{getter_name}";
    __property_info.setter_name = "{setter_name}";

    __class_info.properties["{prop_name}"] = __property_info;
}}
""")

        append(class_src, c_indent,
               f"classes->set({class_idx}, __class_info);")

        c_indent -= 1
        append(class_src, c_indent, "}\n")

        # __namecall
        if "methods" in g_class:
            append(class_src, c_indent, f"""\
// __namecall
lua_pushstring(L, "{class_name}");
lua_pushinteger(L, {class_idx});
lua_pushlightuserdata(L, classes);
lua_pushcclosure(L, luaGD_class_namecall, "{metatable_name}.__namecall", 3);
lua_setfield(L, -4, "__namecall");
""")

        if "properties" in g_class:
            # __index
            append(class_src, c_indent, f"""\
// __index
lua_pushstring(L, "{class_name}");
lua_pushinteger(L, {class_idx});
lua_pushlightuserdata(L, classes);
lua_pushcclosure(L, luaGD_class_index, "{metatable_name}.__index", 3);
lua_setfield(L, -4, "__index");
""")

            # __newindex
            append(class_src, c_indent, f"""\
// __newindex
lua_pushstring(L, "{class_name}");
lua_pushinteger(L, {class_idx});
lua_pushlightuserdata(L, classes);
lua_pushcclosure(L, luaGD_class_newindex, "{metatable_name}.__newindex", 3);
lua_setfield(L, -4, "__newindex");
""")

        # Integer constants
        if "constants" in g_class:
            append(class_src, c_indent, "// Constants")

            for constant in g_class["constants"]:
                append(class_src, c_indent, f"""\
lua_pushinteger(L, {constant["value"]});
lua_setfield(L, -3, "{constant["name"]}");
""")

        # Enums
        if "enums" in g_class:
            append(class_src, c_indent,
                   common.generate_enums(g_class["enums"]))

        # Constructors
        ctor_name = "luaGD_class_no_ctor"
        if g_class["is_instantiable"] and not class_name == "ClassDB":
            ctor_name = "luaGD_class_ctor"

        append(class_src, c_indent, f"""\
// Constructor
lua_pushstring(L, "{class_name}");
lua_pushcclosure(L, {ctor_name}, "{class_name}.__call", 1);
lua_setfield(L, -2, "__call");
""")

        # Global __index
        append(class_src, c_indent, f"""\
// Global __index
lua_pushstring(L, "{class_name}");
lua_pushinteger(L, {class_idx});
lua_pushlightuserdata(L, classes);
lua_pushcclosure(L, luaGD_class_global_index, "{class_name}.__index", 3);
lua_setfield(L, -2, "__index");
""")

        # Singleton getter
        singleton_matches = [s for s in singletons if s["type"] == class_name]
        if len(singleton_matches) > 0:
            singleton = singleton_matches[0]

            append(class_src, c_indent, f"""\
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
        append(class_src, c_indent, "luaGD_poplib(L, true);")

        c_indent -= 1
        append(class_src, c_indent, "}")

        if g_class != classes[-1]:
            class_src.append("")

        # Save class file
        utils.write_file(get_src_path(src_dir, class_name), class_src)

    src.append("""
    __first_init = false;
}\
""")

    # Save
    utils.write_file(src_dir / "luagd_classes.gen.cpp", src)
    utils.write_file(include_dir / "luagd_classes.gen.h", header)
