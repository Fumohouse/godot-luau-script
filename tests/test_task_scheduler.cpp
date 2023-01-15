#include <catch_amalgamated.hpp>

#include <lua.h>

#include "luau_script.h"
#include "task_scheduler.h"
#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "task scheduler: basic functionality") {
    GDLuau gd_luau;
    TaskScheduler &task_scheduler = LuauLanguage::get_singleton()->get_task_scheduler();

    lua_State *L = gd_luau.get_vm(GDLuau::VM_CORE);

    SECTION("wait") {
        lua_State *T = lua_newthread(L);

        ASSERT_EVAL_OK(T, "wait(1)")

        REQUIRE(lua_status(T) == LUA_YIELD);
        task_scheduler.frame(0.5);
        REQUIRE(lua_status(T) == LUA_YIELD);
        task_scheduler.frame(0.6);
        REQUIRE(lua_status(T) == LUA_OK);
    }
}
