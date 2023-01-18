#pragma once

#include <lua.h>

#define LUAGD_LOAD_GUARD(L, key)             \
    lua_getfield(L, LUA_REGISTRYINDEX, key); \
                                             \
    if (!lua_isnil(L, -1))                   \
        return;                              \
                                             \
    lua_pop(L, 1);                           \
                                             \
    lua_pushboolean(L, true);                \
    lua_setfield(L, LUA_REGISTRYINDEX, key);

template <typename T>
T *luaGD_lightudataup(lua_State *L, int index) {
    return reinterpret_cast<T *>(
            lua_tolightuserdata(L, lua_upvalueindex(index)));
}

void luaGD_openbuiltins(lua_State *L);
void luaGD_openclasses(lua_State *L);
void luaGD_openglobals(lua_State *L);
