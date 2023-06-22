#pragma once

#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <lua.h>

using namespace godot;

class ApiEnum;

// ! Must update ApiEnum and luau_analysis whenever this is changed
enum ThreadPermissions {
    PERMISSION_INHERIT = -1,
    PERMISSION_BASE = 0,
    // Default permission level. Restricted to core.
    PERMISSION_INTERNAL = 1 << 0,
    PERMISSION_OS = 1 << 1,
    PERMISSION_FILE = 1 << 2,
    PERMISSION_HTTP = 1 << 3
};

void luaGD_checkpermissions(lua_State *L, const char *p_name, BitField<ThreadPermissions> p_permissions);
