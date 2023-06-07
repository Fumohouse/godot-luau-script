#pragma once

#include <lua.h>
#include <lualib.h>

#include "luagd_stack.h"

#define luaGD_objnullerror(L, p_i) luaL_error(L, "argument #%d: Object is null or freed", p_i)
#define luaGD_indexerror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid member of %s", p_key, p_of)
#define luaGD_nomethoderror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid method of %s", p_key, p_of)
#define luaGD_readonlyerror(L, p_type) luaL_error(L, "type '%s' is read-only", p_type)
#define luaGD_nonamecallatomerror(L) luaL_error(L, "no namecallatom")

#define luaGD_valueerror(L, p_key, p_got, p_expected) luaL_error(L, "invalid type for value of key %s: got %s, expected %s", p_key, p_got, p_expected)

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int p_index, const char *p_key, lua_Type p_texpected) {
    if (!LuaStackOp<T>::is(L, p_index))
        luaGD_valueerror(L, p_key, lua_typename(L, p_texpected), luaL_typename(L, p_index));

    T val = LuaStackOp<T>::get(L, p_index);

    lua_pop(L, 1);
    return val;
}
