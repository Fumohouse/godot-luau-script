#pragma once

#include <lua.h>

#include "luagd.h"

#define LUA_BUILTIN_CONST(variant_type, const_name, const_type)                                                              \
    [](lua_State *L) -> int                                                                                                  \
    {                                                                                                                        \
        static bool __did_init;                                                                                              \
        static Variant __const_value;                                                                                        \
                                                                                                                             \
        if (!__did_init)                                                                                                     \
        {                                                                                                                    \
            __did_init = true;                                                                                               \
            internal::gdn_interface->variant_get_constant_value(GDNATIVE_VARIANT_TYPE_VECTOR2, #const_name, &__const_value); \
        }                                                                                                                    \
                                                                                                                             \
        LuaStackOp<const_type>::push(L, __const_value);                                                                      \
        return 1;                                                                                                            \
    }

// The corresponding source file for this method is generated.
void luaGD_openbuiltins(lua_State *L);

int luaGD_builtin_namecall(lua_State *L);
int luaGD_builtin_global_index(lua_State *L);
