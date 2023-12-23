#include "luagd_permissions.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>

#include "error_strings.h"
#include "extension_api.h"
#include "luagd_lib.h"

void luaGD_checkpermissions(lua_State *L, const char *p_name, BitField<ThreadPermissions> p_permissions) {
    GDThreadData *udata = luaGD_getthreaddata(L);

    if ((udata->permissions & p_permissions) != p_permissions) {
        luaL_error(
                L, PERMISSIONS_ERR,
                p_name, p_permissions.operator int64_t(), udata->permissions.operator int64_t());
    }
}

const ApiEnum &get_permissions_enum() {
    static ApiEnum e = {
        "Permissions",
        false,
        {
                { "BASE", PERMISSION_BASE },
                { "INTERNAL", PERMISSION_INTERNAL },
                { "OS", PERMISSION_OS },
                { "FILE", PERMISSION_FILE },
                { "HTTP", PERMISSION_HTTP },
        }
    };

    return e;
}
