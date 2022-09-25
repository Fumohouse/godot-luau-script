#include "test_utils.h"

#include <lua.h>
#include <Luau/Compiler.h>
#include <string>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include "luagd.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

LuauFixture::LuauFixture()
{
    L = luaGD_newstate(PERMISSION_BASE);
}

LuauFixture::~LuauFixture()
{
    if (L != nullptr)
    {
        luaGD_close(L);
        L = nullptr;
    }
}

ExecOutput luaGD_exec(lua_State *L, const char *src)
{
    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(src, opts);

    ExecOutput output;

    if (luau_load(L, "=exec", bytecode.data(), bytecode.size(), 0) == 0)
    {
        int status = lua_resume(L, nullptr, 0);

        if (status == LUA_OK)
        {
            output.status = OK;
            return output;
        }
        else if (status == LUA_YIELD)
        {
            output.status = FAILED;
            output.error = "Unexpected yield";
            return output;
        }

        String error = LuaStackOp<String>::get(L, -1);
        lua_pop(L, 1);

        output.status = FAILED;
        output.error = error;
        return output;
    }

    output.status = FAILED;
    output.error = LuaStackOp<String>::get(L, -1);
    lua_pop(L, 1);

    return output;
}