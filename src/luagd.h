#pragma once

#include <lua.h>
#include <cstdlib>

#include "luagd_stack.h"

lua_State *luaGD_newstate();

template <typename T>
void luaGD_push(lua_State *L, const T &value)
{
    LuaStackOp<T>::push(L, value);
}

template <typename T>
T luaGD_get(lua_State *L, int index)
{
    return LuaStackOp<T>::get(L, index);
}
