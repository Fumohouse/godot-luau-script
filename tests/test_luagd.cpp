#include <catch_amalgamated.hpp>

#include <lua.h>

#include "gd_luau.h"
#include "luagd.h"
#include "luagd_permissions.h"

TEST_CASE("vm: permissions") {
    lua_State *L = luaGD_newstate(GDLuau::VM_MAX, PERMISSION_INTERNAL);
    GDThreadData *udata = luaGD_getthreaddata(L);

    SECTION("vm initialized") {
        REQUIRE(udata);
        REQUIRE(udata->permissions == PERMISSION_INTERNAL);
    }

    SECTION("threads inherit permissions") {
        lua_State *T = lua_newthread(L);
        GDThreadData *thread_udata = luaGD_getthreaddata(T);

        REQUIRE(thread_udata);
        REQUIRE(thread_udata != udata);
        REQUIRE(thread_udata->permissions == PERMISSION_INTERNAL);

        lua_pop(L, 1);
    }

    SECTION("thread permission initialization") {
        lua_State *T = luaGD_newthread(L, PERMISSION_FILE);
        GDThreadData *thread_udata = luaGD_getthreaddata(T);

        REQUIRE(thread_udata);
        REQUIRE(thread_udata != udata);
        REQUIRE(thread_udata->permissions == PERMISSION_FILE);

        lua_pop(L, 1);
    }

    luaGD_close(L);
}
