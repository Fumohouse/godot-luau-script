#pragma once

#include <lua.h>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>

#include "luagd_permissions.h"

using namespace godot;

struct GDThreadData
{
    BitField<ThreadPermissions> permissions = 0;
};

lua_State *luaGD_newstate(ThreadPermissions base_permissions);
lua_State *luaGD_newthread(lua_State *L, ThreadPermissions permissions);
void luaGD_close(lua_State *L);

GDThreadData *luaGD_getthreaddata(lua_State *L);
