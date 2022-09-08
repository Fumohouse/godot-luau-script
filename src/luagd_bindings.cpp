#include "luagd_bindings.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/classes/object.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

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
