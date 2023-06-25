#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/type_info.hpp>

#include "error_strings.h"
#include "gd_luau.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luau_script.h"

using namespace godot;

#define LUAU_LOCK(L) MutexLock L##_lock(*luaGD_getthreaddata(L)->lock.ptr())

struct GDThreadData {
    GDLuau::VMType vm_type = GDLuau::VM_MAX;
    BitField<ThreadPermissions> permissions = 0;
    Ref<LuauScript> script;
    Ref<Mutex> lock;

    uint64_t interrupt_deadline = 0;
};

lua_State *luaGD_newstate(GDLuau::VMType p_vm_type, BitField<ThreadPermissions> p_base_permissions);
lua_State *luaGD_newthread(lua_State *L, BitField<ThreadPermissions> p_permissions);
GDThreadData *luaGD_getthreaddata(lua_State *L);
void luaGD_close(lua_State *L);

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int p_index, const char *p_key, lua_Type p_texpected) {
    if (!LuaStackOp<T>::is(L, p_index))
        luaGD_valueerror(L, p_key, lua_typename(L, p_texpected), luaL_typename(L, p_index));

    T val = LuaStackOp<T>::get(L, p_index);

    lua_pop(L, 1);
    return val;
}

void luaGD_openlibs(lua_State *L);
