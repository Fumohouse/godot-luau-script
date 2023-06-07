#include "luagd_utils.h"

#include <lua.h>

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key) {
    lua_getfield(L, p_index, p_key);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    return true;
}
