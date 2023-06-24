#pragma once

#include <lua.h>
#include <lualib.h>

#include "error_strings.h"
#include "luagd_stack.h"

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int p_index, const char *p_key, lua_Type p_texpected) {
    if (!LuaStackOp<T>::is(L, p_index))
        luaGD_valueerror(L, p_key, lua_typename(L, p_texpected), luaL_typename(L, p_index));

    T val = LuaStackOp<T>::get(L, p_index);

    lua_pop(L, 1);
    return val;
}
