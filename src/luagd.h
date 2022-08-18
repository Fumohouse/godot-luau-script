#pragma once

#include <lua.h>
#include <cstdlib>
#include <unordered_map>
#include <string>

#include "luagd_stack.h"
#include "luagd_builtins_stack.gen.h" // this is just a little bit janky

// TODO: May want to use Godot's HashMap, except right now the godot-cpp version doesn't compile
typedef std::unordered_map<std::string, lua_CFunction> MethodMap;

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

lua_State *luaGD_newstate();

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name);

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

template <typename T>
bool luaGD_is(lua_State *L, int index)
{
    return LuaStackOp<T>::is(L, index);
}

template <typename T>
T luaGD_check(lua_State *L, int index)
{
    return LuaStackOp<T>::check(L, index);
}
