#include "luagd_bindings.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>

#include "extension_api.h"
#include "luagd_variant.h"
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

namespace godot
{
    class Object;
}

/////////////
// Generic //
/////////////

static void push_enum(lua_State *L, const ApiEnum &p_enum) // notation cause reserved keyword
{
    lua_createtable(L, 0, p_enum.values.size());

    for (const Pair<String, int32_t> &value : p_enum.values)
    {
        lua_pushinteger(L, value.second);
        lua_setfield(L, -2, value.first.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
}

/////////////
// Globals //
/////////////

static int luaGD_utility_function(lua_State *L)
{
    const ApiUtilityFunction *func = luaGD_lightudataup<ApiUtilityFunction>(L, 1);

    int nargs = lua_gettop(L);

    Vector<Variant> varargs;
    Vector<LuauVariant> args;

    Vector<const void *> pargs;
    pargs.resize(nargs);

    if (func->is_vararg)
    {
        varargs.resize(nargs);

        for (int i = 0; i < nargs; i++)
        {
            Variant arg = LuaStackOp<Variant>::check(L, i + 1);

            if (i < func->arguments.size())
            {
                Variant::Type expected_type = (Variant::Type)func->arguments[i].type;

                if (expected_type != Variant::NIL && arg.get_type() != expected_type)
                    luaL_typeerrorL(L, i + 1, Variant::get_type_name(expected_type).utf8().get_data());
            }

            varargs.set(i, arg);
            pargs.set(i, &varargs[i]);
        }
    }
    else
    {
        args.resize(nargs);

        for (int i = 0; i < nargs; i++)
        {
            LuauVariant arg;
            arg.lua_check(L, i + 1, func->arguments[i].type);
            args.set(i, arg);
            pargs.set(i, args[i].get_opaque_pointer());
        }
    }

    LuauVariant ret;
    ret.initialize(func->return_type);

    func->func(ret.get_opaque_pointer(), pargs.ptr(), nargs);

    if (func->return_type == GDEXTENSION_VARIANT_TYPE_NIL)
    {
        return 0;
    }
    else
    {
        ret.lua_push(L);
        return 1;
    }
}

void luaGD_openglobals(lua_State *L)
{
    LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")

    const ExtensionApi &api = get_extension_api();

    // Enum
    lua_createtable(L, 0, api.global_enums.size());

    for (const ApiEnum &global_enum : api.global_enums)
    {
        push_enum(L, global_enum);
        lua_setfield(L, -2, global_enum.name.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Enum");

    // Constants
    // does this work? idk
    lua_createtable(L, 0, api.global_constants.size());

    for (const ApiConstant &global_constant : api.global_constants)
    {
        lua_pushinteger(L, global_constant.value);
        lua_setfield(L, -2, global_constant.name.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Constants");

    // Utility functions
    for (const ApiUtilityFunction &utility_function : api.utility_functions)
    {
        lua_pushlightuserdata(L, (void *)&utility_function);
        lua_pushcclosure(L, luaGD_utility_function, utility_function.debug_name, 1);
        lua_setglobal(L, utility_function.name.utf8().get_data());
    }
}

static int luaGD_variant_tostring(lua_State *L)
{
    // Special case - freed objects
    if (LuaStackOp<Object *>::is(L, 1) && LuaStackOp<Object *>::get(L, 1) == nullptr)
        lua_pushstring(L, "<Freed Object>");
    else
    {
        Variant v = LuaStackOp<Variant>::check(L, 1);
        String str = v.stringify();
        lua_pushstring(L, str.utf8().get_data());
    }

    return 1;
}

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name)
{
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L);               // global table
    lua_createtable(L, 0, 3);      // global metatable - assume 3 fields: __fortype, __call, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    lua_pushcfunction(L, luaGD_variant_tostring, "Variant.__tostring");
    lua_setfield(L, -4, "__tostring");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}

void luaGD_poplib(lua_State *L, bool is_obj)
{
    if (is_obj)
    {
        lua_pushboolean(L, true);
        lua_setfield(L, -4, "__isgdobj");
    }

    // global will be set readonly on sandbox
    lua_setreadonly(L, -3, true); // instance metatable
    lua_setreadonly(L, -1, true); // global metatable

    lua_pop(L, 3);
}
