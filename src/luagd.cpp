#include "luagd.h"

#include <lua.h>
#include <lualib.h>
#include <cstdlib>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/type_info.hpp>

#include "gd_luau.h"
#include "luagd_bindings.h"
#include "luagd_permissions.h"

using namespace godot;

// Based on the default implementation seen in the Lua 5.1 reference
static void *luaGD_alloc(void *, void *p_ptr, size_t, size_t p_nsize) {
    if (p_nsize == 0) {
        // Lua assumes free(NULL) is ok. For Godot it is not.
        if (p_ptr)
            memfree(p_ptr);

        return nullptr;
    }

    return memrealloc(p_ptr, p_nsize);
}

static GDThreadData *luaGD_initthreaddata(lua_State *LP, lua_State *L) {
    GDThreadData *udata = memnew(GDThreadData);
    lua_setthreaddata(L, udata);

    if (LP) {
        GDThreadData *parent_udata = luaGD_getthreaddata(LP);
        udata->vm_type = parent_udata->vm_type;
        udata->permissions = parent_udata->permissions;
        udata->script = parent_udata->script;
        udata->lock = parent_udata->lock;
    }

    return udata;
}

static void luaGD_userthread(lua_State *LP, lua_State *L) {
    if (LP) {
        luaGD_initthreaddata(LP, L);
    } else {
        GDThreadData *udata = luaGD_getthreaddata(L);
        if (udata) {
            lua_setthreaddata(L, nullptr);
            memdelete(udata);
        }
    }
}

lua_State *luaGD_newstate(GDLuau::VMType p_vm_type, BitField<ThreadPermissions> p_base_permissions) {
    lua_State *L = lua_newstate(luaGD_alloc, nullptr);

    luaL_openlibs(L);
    luaGD_openbuiltins(L);
    luaGD_openclasses(L);
    luaGD_openglobals(L);

    GDThreadData *udata = luaGD_initthreaddata(nullptr, L);
    udata->vm_type = p_vm_type;
    udata->permissions = p_base_permissions;
    udata->lock.instantiate();

    lua_Callbacks *callbacks = lua_callbacks(L);
    callbacks->userthread = luaGD_userthread;

    return L;
}

lua_State *luaGD_newthread(lua_State *L, BitField<ThreadPermissions> p_permissions) {
    lua_State *T = lua_newthread(L);

    GDThreadData *udata = luaGD_getthreaddata(T);
    udata->permissions = p_permissions;

    return T;
}

void luaGD_close(lua_State *L) {
    L = lua_mainthread(L);

    GDThreadData *udata = luaGD_getthreaddata(L);
    if (udata) {
        lua_setthreaddata(L, nullptr);
        memdelete(udata);
    }

    lua_close(L);
}

GDThreadData *luaGD_getthreaddata(lua_State *L) {
    return reinterpret_cast<GDThreadData *>(lua_getthreaddata(L));
}
