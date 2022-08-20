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
