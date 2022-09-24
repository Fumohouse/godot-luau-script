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
lua_State *luaGD_newthread(lua_State *L, ThreadPermissions permissions);
void luaGD_close(lua_State *L);

GDThreadData *luaGD_getthreaddata(lua_State *L);
