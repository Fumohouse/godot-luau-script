#pragma once

#include <lua.h>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/godot.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

#define LUA_BUILTIN_CONST(variant_type, const_name, const_type)                                             \
    {                                                                                                       \
        static bool __did_init;                                                                             \
        static Variant __const_value;                                                                       \
                                                                                                            \
        if (!__did_init)                                                                                    \
        {                                                                                                   \
            __did_init = true;                                                                              \
            internal::gdn_interface->variant_get_constant_value(variant_type, #const_name, &__const_value); \
        }                                                                                                   \
                                                                                                            \
        LuaStackOp<const_type>::push(L, __const_value);                                                     \
        lua_setfield(L, -3, #const_name);                                                                   \
    }

int luaGD_builtin_namecall(lua_State *L);
int luaGD_builtin_newindex(lua_State *L);
int luaGD_builtin_global_index(lua_State *L);
