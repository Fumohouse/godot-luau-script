#include "luagd.h"

#include <lua.h>
#include <lualib.h>
#include <cstdlib>
#include <godot_cpp/core/memory.hpp>

#include "luagd_bindings.h"
#include "luagd_permissions.h"

using namespace godot;

// Based on the default implementation seen in the Lua 5.1 reference
static void *luaGD_alloc(void *, void *ptr, size_t, size_t nsize) {
    if (nsize == 0) {
        // Lua assumes free(NULL) is ok. For Godot it is not.
        if (ptr != nullptr)
            memfree(ptr);

        return nullptr;
    }

    return memrealloc(ptr, nsize);
}

static GDThreadData *luaGD_initthreaddata(lua_State *LP, lua_State *L) {
    GDThreadData *udata = memnew(GDThreadData);
    lua_setthreaddata(L, udata);

    if (LP != nullptr)
        udata->permissions = luaGD_getthreaddata(LP)->permissions;

    return udata;
}

static void luaGD_userthread(lua_State *LP, lua_State *L) {
    if (LP != nullptr) {
        luaGD_initthreaddata(LP, L);
    } else {
        GDThreadData *udata = luaGD_getthreaddata(L);
        if (udata != nullptr) {
            lua_setthreaddata(L, nullptr);
            memdelete(udata);
        }
    }
}

lua_State *luaGD_newstate(ThreadPermissions base_permissions) {
    lua_State *L = lua_newstate(luaGD_alloc, nullptr);

    luaL_openlibs(L);
    luaGD_openbuiltins(L);
    luaGD_openclasses(L);
    luaGD_openglobals(L);

    GDThreadData *udata = luaGD_initthreaddata(nullptr, L);
    udata->permissions = base_permissions;

    lua_Callbacks *callbacks = lua_callbacks(L);
    callbacks->userthread = luaGD_userthread;

    return L;
}

lua_State *luaGD_newthread(lua_State *L, ThreadPermissions permissions) {
    lua_State *T = lua_newthread(L);

    GDThreadData *udata = luaGD_getthreaddata(T);
    udata->permissions = permissions;

    return T;
}

void luaGD_close(lua_State *L) {
    L = lua_mainthread(L);

    GDThreadData *udata = luaGD_getthreaddata(L);
    if (udata != nullptr) {
        lua_setthreaddata(L, nullptr);
        memdelete(udata);
    }

    lua_close(L);
}

GDThreadData *luaGD_getthreaddata(lua_State *L) {
    return reinterpret_cast<GDThreadData *>(lua_getthreaddata(L));
}
