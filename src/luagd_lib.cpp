#include "luagd_lib.h"

#include <lua.h>
#include <lualib.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gd_luau.h"
#include "luagd_bindings.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "wrapped_no_binding.h"

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

GDThreadData *luaGD_getthreaddata(lua_State *L) {
    return reinterpret_cast<GDThreadData *>(lua_getthreaddata(L));
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

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key) {
    lua_getfield(L, p_index, p_key);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    return true;
}

/* GLOBAL */

template <typename T>
static int luaGD_str_ctor(lua_State *L) {
    String str = LuaStackOp<String>::check(L, 1);
    LuaStackOp<T>::push(L, str, true);
    return 1;
}

static int luaGD_load(lua_State *L) {
    String path = LuaStackOp<String>::check(L, 1);
    GDThreadData *udata = luaGD_getthreaddata(L);

    if (!path.begins_with("res://") && !path.begins_with("user://")) {
        path = udata->script->get_path().get_base_dir().path_join(path);
    }

    Ref<Resource> res = nb::ResourceLoader::get_singleton_nb()->load(path);
    LuaStackOp<Object *>::push(L, res.ptr());
    return 1;
}

static int luaGD_gdtypeof(lua_State *L) {
    luaL_checkany(L, 1);

    int type = LuaStackOp<Variant>::get_type(L, 1);
    if (type == -1) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, type);
    }

    return 1;
}

static const luaL_Reg global_funcs[] = {
    { "SN", luaGD_str_ctor<StringName> },
    { "NP", luaGD_str_ctor<NodePath> },

    { "load", luaGD_load },

    { "gdtypeof", luaGD_gdtypeof },

    { nullptr, nullptr }
};

void luaGD_openlibs(lua_State *L) {
    luaL_register(L, "_G", global_funcs);
}
