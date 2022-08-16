#include "luagd.h"

#include <godot_cpp/core/memory.hpp>
#include <lua.h>
#include <lualib.h>
#include <cstdlib>

using namespace godot;

// Based on the default implementation seen in the Lua 5.1 reference
static void *luaGD_alloc(void *, void *ptr, size_t, size_t nsize)
{
    if (nsize == 0)
    {
        // Lua assumes free(NULL) is ok. For Godot it is not.
        if (ptr != nullptr)
            memfree(ptr);

        return nullptr;
    }

    return memrealloc(ptr, nsize);
}

lua_State *luaGD_newstate()
{
    return lua_newstate(luaGD_alloc, nullptr);
}

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name)
{
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L); // global table
    lua_createtable(L, 0, 3); // global metatable - assume 3 fields: __fortype, __call, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}
