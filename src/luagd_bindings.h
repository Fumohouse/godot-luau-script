#pragma once

#include <lua.h>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/templates/hash_map.hpp>

typedef godot::HashMap<godot::StringName, lua_CFunction> MethodMap;

template <typename T>
T *luaGD_lightudataup(lua_State *L, int index)
{
    return reinterpret_cast<T *>(
        lua_tolightuserdata(L, lua_upvalueindex(index)));
}

// The corresponding source files for these methods are generated.
void luaGD_openbuiltins(lua_State *L);
void luaGD_openclasses(lua_State *L);
void luaGD_openglobals(lua_State *L);

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name);
void luaGD_poplib(lua_State *L, bool is_obj);
