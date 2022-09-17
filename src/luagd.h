#pragma once

#include <lua.h>
#include <godot_cpp/core/type_info.hpp>

#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h" // this is just a little bit janky

struct GDThreadData
{
    BitField<ThreadPermissions> permissions = 0;
};

lua_State *luaGD_newstate(ThreadPermissions base_permissions);
void luaGD_close(lua_State *L);

GDThreadData *luaGD_getthreaddata(lua_State *L);

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
