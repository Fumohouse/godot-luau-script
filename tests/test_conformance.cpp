#include <catch_amalgamated.hpp>

#include <lua.h>
#include <lualib.h>
#include <fstream>
#include <godot_cpp/variant/string.hpp>
#include <ios>
#include <sstream>
#include <string>

#include "gd_luau.h"
#include "luagd.h"
#include "luagd_permissions.h"
#include "luau_cache.h"
#include "test_utils.h"

static int lua_gccollect(lua_State *L) {
    lua_gc(L, LUA_GCCOLLECT, 0);
    return 0;
}

static int lua_asserterror(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    String expected_err = luaL_optstring(L, 2, "");

    lua_pushvalue(L, 1);
    int status = lua_pcall(L, 0, 0, 0);
    if (status == LUA_OK || status == LUA_YIELD || status == LUA_BREAK) {
        luaL_error(L, "assertion failed! function did not error");
    }

    if (!expected_err.is_empty()) {
        String err = LuaStackOp<String>::get(L, -1);
        String err_trimmed = String(":").join(err.split(":").slice(2)).substr(1);

        if (err_trimmed != expected_err) {
            luaL_error(L, "assertion failed! error doesn't match: expected '%s', got '%s'.",
                    expected_err.utf8().get_data(),
                    err_trimmed.utf8().get_data());
        }
    }

    return 0;
}

static const luaL_Reg test_lib[] = {
    { "gccollect", lua_gccollect },
    { "asserterror", lua_asserterror },
    { nullptr, nullptr }
};

static ExecOutput runConformance(const char *name) {
    // Borrowed from Luau (license information in README.md).
    std::string path = std::string("../") + __FILE__;
    path.erase(path.find_last_of("\\/"));
    path += "/conformance/";
    path += name;

    std::ifstream stream(path, std::ios::in | std::ios::binary);
    REQUIRE(stream);

    std::stringstream buffer;
    buffer << stream.rdbuf();

    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
    lua_State *T = luaGD_newthread(L, PERMISSION_INTERNAL);
    luaL_sandboxthread(T);

    const luaL_Reg *lib = test_lib;
    while (lib->func) {
        lua_pushcfunction(T, lib->func, lib->name);
        lua_setglobal(T, lib->name);

        lib++;
    }

    return luaGD_exec(T, buffer.str().c_str());
}

#define CONFORMANCE_TEST(name, file)           \
    TEST_CASE("conformance: " name) {          \
        GDLuau gd_luau;                        \
        LuauCache luau_cache;                  \
                                               \
        ExecOutput out = runConformance(file); \
        if (out.status != OK)                  \
            FAIL(out.error.utf8().get_data()); \
    }

CONFORMANCE_TEST("class bindings", "ClassBindings.lua")
CONFORMANCE_TEST("global bindings", "GlobalBindings.lua")
CONFORMANCE_TEST("builtin bindings", "BuiltinBindings.lua")
CONFORMANCE_TEST("luagd lib", "LuaGDLib.lua")
CONFORMANCE_TEST("script instance", "LuauScriptInstance.lua")
