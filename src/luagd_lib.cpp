#include "luagd_lib.h"

#include <lua.h>
#include <lualib.h>
#include <cstddef>
#include <cstring>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_stack.h"
#include "wrapped_no_binding.h"

using namespace godot;

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

/* OPEN */

void luaGD_openlibs(lua_State *L) {
    luaL_register(L, "_G", global_funcs);
}
