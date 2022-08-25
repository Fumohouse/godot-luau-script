#include "luagd_builtins.h"

#include <lua.h>
#include <lualib.h>

#include "luagd_bindings.h"

int luaGD_builtin_namecall(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));

    MethodMap *methods = luaGD_lightudataup<MethodMap>(L, 2);

    if (const char *name = lua_namecallatom(L, nullptr))
    {
        if (methods->count(name) > 0)
            return (methods->at(name))(L);

        luaL_error(L, "%s is not a valid method of %s", name, class_name);
    }

    luaL_error(L, "no namecallatom");
}

int luaGD_builtin_newindex(lua_State *L)
{
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));
    luaL_error(L, "%s is readonly", class_name);
}

int luaGD_builtin_global_index(lua_State *L)
{
    const char *class_name =
        lua_tostring(L, lua_upvalueindex(1));

    const char *key = luaL_checkstring(L, 2);

    // Static functions
    MethodMap *statics = luaGD_lightudataup<MethodMap>(L, 2);
    if (statics && statics->count(key) > 0)
    {
        lua_pushcfunction(L, statics->at(key), key);
        return 1;
    }

    // Instance methods
    MethodMap *methods = luaGD_lightudataup<MethodMap>(L, 3);
    if (methods && methods->count(key) > 0)
    {
        lua_pushcfunction(L, methods->at(key), key);
        return 1;
    }

    luaL_error(L, "%s is not a valid member of %s", key, class_name);
}
