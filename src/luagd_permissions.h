#pragma once

#include <lua.h>

enum ThreadPermissions
{
    PERMISSION_BASE = 0,
    // Default permission level. Restricted to core.
    PERMISSION_INTERNAL = 1 << 0,
    PERMISSION_OS = 1 << 1,
    PERMISSION_FILE = 1 << 2,
    PERMISSION_HTTP = 1 << 3
};

void luaGD_checkpermissions(lua_State *L, const char *name, ThreadPermissions permissions);
