#include "luagd_builtins.h"

#include <lua.h>
#include <lualib.h>

static MethodMap *luaGD_getmethodmap(lua_State *L, int index)
{
    return reinterpret_cast<MethodMap *>(
        lua_tolightuserdata(L, lua_upvalueindex(index))
    );
}

int luaGD_builtin_namecall(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    MethodMap *methods = luaGD_getmethodmap(L, 2);

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (methods->count(name) > 0)
            return (methods->at(name))(L);

        luaL_error(L, "%s is not a valid method of %s", name, class_name);
    }

    luaL_error(L, "no namecallatom");
}

int luaGD_builtin_global_index(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    MethodMap *statics = luaGD_getmethodmap(L, 2);
    MethodMap *methods = luaGD_getmethodmap(L, 3);

    const char *key = luaL_checkstring(L, 2);

    if (statics && statics->count(key) > 0)
    {
        lua_pushcfunction(L, statics->at(key), key);
        return 1;
    }

    if (methods && methods->count(key) > 0)
    {
        lua_pushcfunction(L, methods->at(key), key);
        return 1;
    }

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
}
