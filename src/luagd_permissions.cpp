#include "luagd_permissions.h"

#include <lua.h>
#include <lualib.h>

#include "extension_api.h"
#include "luagd.h"

void luaGD_checkpermissions(lua_State *L, const char *name, ThreadPermissions permissions) {
    GDThreadData *udata = luaGD_getthreaddata(L);

    if ((udata->permissions & permissions) != permissions) {
        luaL_error(
                L,
                "!!! THREAD PERMISSION VIOLATION: attempted to access %s. needed permissions: %d, got: %li !!!",
                name, permissions, udata->permissions.operator int64_t());
    }
}

const ApiEnum &get_permissions_enum() {
    static ApiEnum e;
    static bool did_init = false;

    if (!did_init) {
        e.name = "Permissions";
        e.is_bitfield = true;
        e.values = {
            { "BASE", PERMISSION_BASE },
            { "INTERNAL", PERMISSION_INTERNAL },
            { "OS", PERMISSION_OS },
            { "FILE", PERMISSION_FILE },
            { "HTTP", PERMISSION_HTTP },
        };

        did_init = true;
    }

    return e;
}
