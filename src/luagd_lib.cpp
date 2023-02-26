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

using namespace godot;

/* STRING */

#define LUAGD_STREXT_COMPARISON_ARGS                       \
    size_t self_len;                                       \
    const char *self = luaL_checklstring(L, 1, &self_len); \
                                                           \
    size_t cmp_len;                                        \
    const char *cmp = luaL_checklstring(L, 2, &cmp_len);   \
                                                           \
    if (cmp_len > self_len) {                              \
        lua_pushboolean(L, false);                         \
        return 1;                                          \
    }

static int luaGD_strext_startswith(lua_State *L) {
    LUAGD_STREXT_COMPARISON_ARGS

    lua_pushboolean(L, strncmp(self, cmp, cmp_len) == 0);
    return 1;
}

static int luaGD_strext_endswith(lua_State *L) {
    LUAGD_STREXT_COMPARISON_ARGS

    const char *self_end = self + self_len - cmp_len;
    lua_pushboolean(L, strcmp(self_end, cmp) == 0);
    return 1;
}

static const luaL_Reg string_ext[] = {
    { "startswith", luaGD_strext_startswith },
    { "endswith", luaGD_strext_endswith },
    { nullptr, nullptr }
};

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

    Ref<Resource> res = ResourceLoader::get_singleton()->load(path);
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
    luaL_register(L, "strext", string_ext);
    luaL_register(L, "_G", global_funcs);
}
