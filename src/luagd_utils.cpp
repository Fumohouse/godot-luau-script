#include "luagd_utils.h"

bool luaGD_getfield(lua_State *L, int index, const char *key)
{
    lua_getfield(L, index, key);

    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }

    return true;
}
