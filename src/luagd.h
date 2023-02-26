#pragma once

#include <lua.h>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/core/mutex_lock.hpp>

#include "gd_luau.h"
#include "luagd_permissions.h"
#include "luau_script.h"

using namespace godot;

struct GDThreadData {
    GDLuau::VMType vm_type = GDLuau::VM_MAX;
    BitField<ThreadPermissions> permissions = 0;
    Ref<LuauScript> script;
    Ref<Mutex> lock;
};

lua_State *luaGD_newstate(GDLuau::VMType vm_type, BitField<ThreadPermissions> base_permissions);
lua_State *luaGD_newthread(lua_State *L, BitField<ThreadPermissions> permissions);
void luaGD_close(lua_State *L);

GDThreadData *luaGD_getthreaddata(lua_State *L);

#define LUAU_LOCK(L) MutexLock L##_lock(*luaGD_getthreaddata(L)->lock.ptr())
