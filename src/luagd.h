#pragma once

#include <lua.h>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

class ApiEnum;

// ! Must update ApiEnum whenever this is changed
enum ThreadPermissions {
    PERMISSION_INHERIT = -1,
    PERMISSION_BASE = 0,
    // Default permission level. Restricted to core.
    PERMISSION_INTERNAL = 1 << 0,
    PERMISSION_OS = 1 << 1,
    PERMISSION_FILE = 1 << 2,
    PERMISSION_HTTP = 1 << 3
};

void luaGD_checkpermissions(lua_State *L, const char *name, ThreadPermissions permissions);
const ApiEnum &get_permissions_enum();
struct GDThreadData {
    BitField<ThreadPermissions> permissions = 0;
    String path;
};

lua_State *luaGD_newstate(ThreadPermissions base_permissions);
lua_State *luaGD_newthread(lua_State *L, ThreadPermissions permissions);
void luaGD_close(lua_State *L);

GDThreadData *luaGD_getthreaddata(lua_State *L);
