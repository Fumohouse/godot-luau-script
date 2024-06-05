#include <catch_amalgamated.hpp>

#include <lua.h>

#include "luau_cache.h"
#include "luau_script.h"
#include "task_scheduler.h"
#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "task scheduler: basic functionality") {
	GDLuau gd_luau;
	TaskScheduler &task_scheduler = LuauLanguage::get_singleton()->get_task_scheduler();

	lua_State *L = gd_luau.get_vm(GDLuau::VM_CORE);

	SECTION("wait") {
		lua_State *T = lua_newthread(L);

		ASSERT_EVAL_OK(T, R"ASDF(
            local t = wait(1)
            assert(t > 0)
        )ASDF")

		REQUIRE(lua_status(T) == LUA_YIELD);
		task_scheduler.frame(0.5);
		REQUIRE(lua_status(T) == LUA_YIELD);
		task_scheduler.frame(0.6);
		REQUIRE(lua_status(T) == LUA_OK);
	}
}

TEST_CASE_METHOD(LuauFixture, "task scheduler: wait_signal") {
	luascript_openlibs(L);

	// Load script
	GDLuau gd_luau;
	LuauCache luau_cache;
	TaskScheduler &task_scheduler = LuauLanguage::get_singleton()->get_task_scheduler();

	LOAD_SCRIPT_FILE(script, "instance/Script.lua")

	// Object
	Object *obj = memnew(Object);
	obj->set_script(script);

	LuaStackOp<Object *>::push(L, obj);
	lua_setglobal(L, "testObj");

	SECTION("normal") {
		// Yield
		ASSERT_EVAL_OK(L, R"ASDF(
            local res, a, b = wait_signal(testObj.testSignal)
            assert(res)
            assert(a == 16)
            assert(b == "hello")
        )ASDF")

		REQUIRE(lua_status(L) == LUA_YIELD);

		// Fire
		obj->emit_signal("testSignal", 16, "hello");
		REQUIRE(lua_status(L) == LUA_OK);

		// Remove task from the list (avoid crash in next test case)
		task_scheduler.frame(0.1);
	}

	SECTION("timeout") {
		ASSERT_EVAL_OK(L, R"ASDF(
            local res, a, b = wait_signal(testObj.testSignal, 1)
            assert(res == false)
            assert(a == nil)
            assert(b == nil)
        )ASDF")

		REQUIRE(lua_status(L) == LUA_YIELD);

		task_scheduler.frame(0.5);
		REQUIRE(lua_status(L) == LUA_YIELD);
		task_scheduler.frame(0.6);
		REQUIRE(lua_status(L) == LUA_OK);
	}

	memdelete(obj);
}
