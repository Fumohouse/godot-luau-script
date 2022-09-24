#pragma once

#include <lua.h>
#include <lualib.h>

#include "luagd_stack.h"

bool luaGD_getfield(lua_State *L, int index, const char *key);

template <typename T>
T luaGD_checkkeytype(lua_State *L, int index, const char *key, lua_Type texpected)
{
    if (!LuaStackOp<T>::is(L, index))
        luaL_error(L, "invalid type for key %s: got %s, expected %s", key, lua_typename(L, texpected), lua_typename(L, lua_type(L, index)));

    T val = LuaStackOp<T>::get(L, index);

    lua_pop(L, 1);
    return val;
}
