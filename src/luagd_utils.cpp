#include "luagd_utils.h"

void luaGD_keyerror(lua_State *L, const char *of, const char *got, const char *expected) {
    luaL_error(L, "invalid type for key of %s: got %s, expected %s", of, got, expected);
}

void luaGD_valueerror(lua_State *L, const char *key, const char *got, const char *expected) {
    luaL_error(L, "invalid type for value of key %s: got %s, expected %s", key, got, expected);
}

void luaGD_arrayerror(lua_State *L, const char *of, const char *got, const char *expected) {
    luaL_error(L, "invalid type for %s array: got %s, expected %s", of, got, expected);
}

bool luaGD_getfield(lua_State *L, int index, const char *key) {
    lua_getfield(L, index, key);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    return true;
}
