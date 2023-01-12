#pragma once

#include <lua.h>
#include <lualib.h>

#include "luagd_stack.h"

#define luaGD_typename(L, index) lua_typename(L, lua_type(L, index))

#define luaGD_indexerror(L, key, of) luaL_error(L, "'%s' is not a valid member of '%s'", key, of)
#define luaGD_nomethoderror(L, key, of) luaL_error(L, "'%s' is not a valid method of '%s'", key, of)

#define luaGD_keyerror(L, of, got, expected) luaL_error(L, "invalid type for key of %s: got %s, expected %s", of, got, expected)
#define luaGD_valueerror(L, key, got, expected) luaL_error(L, "invalid type for value of key %s: got %s, expected %s", key, got, expected)
#define luaGD_arrayerror(L, of, got, expected) luaL_error(L, "invalid type for %s array: got %s, expected %s", of, got, expected)

bool luaGD_getfield(lua_State *L, int index, const char *key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int index, const char *key, lua_Type texpected) {
    if (!LuaStackOp<T>::is(L, index))
        luaGD_valueerror(L, key, lua_typename(L, texpected), luaGD_typename(L, index));

    T val = LuaStackOp<T>::get(L, index);

    lua_pop(L, 1);
    return val;
}
