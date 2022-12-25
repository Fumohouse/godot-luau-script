#include "luagd_permissions.h"

#include <lua.h>
#include <lualib.h>

#include "luagd.h"

void luaGD_checkpermissions(lua_State *L, const char *name, ThreadPermissions permissions)
{
    GDThreadData *udata = luaGD_getthreaddata(L);

    if ((udata->permissions & permissions) != permissions)
    {
        luaL_error(
            L,
            "!!! THREAD PERMISSION VIOLATION: attempted to access %s. needed permissions: %d, got: %d !!!",
            name, permissions, udata->permissions.operator int64_t()
        );
    }
}
