#pragma once

#include <lua.h>

#include "luagd.h"

// The corresponding source file for this method is generated.
void luaGD_openbuiltins(lua_State *L);

int luaGD_builtin_namecall(lua_State *L);
int luaGD_builtin_global_index(lua_State *L);
