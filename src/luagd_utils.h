#pragma once

#include <lua.h>
#include <lualib.h>

#include "luagd_stack.h"

#define luaGD_typename(L, index) lua_typename(L, lua_type(L, index))

void luaGD_keyerror(lua_State *L, const char *of, const char *got, const char *expected);
void luaGD_valueerror(lua_State *L, const char *key, const char *got, const char *expected);
void luaGD_arrayerror(lua_State *L, const char *of, const char *got, const char *expected);

bool luaGD_getfield(lua_State *L, int index, const char *key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int index, const char *key, lua_Type texpected)
{
    if (!LuaStackOp<T>::is(L, index))
        luaGD_valueerror(L, key, lua_typename(L, texpected), luaGD_typename(L, index));

    T val = LuaStackOp<T>::get(L, index);

    lua_pop(L, 1);
    return val;
}
