#include "scheduler/scheduler_lib.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/signal.hpp>

#include "core/stack.h"
#include "scheduler/task_scheduler.h"
#include "scheduler/wait_signal_task.h"
#include "scheduler/wait_task.h"
#include "scripting/luau_script.h"

using namespace std;

static int luasched_wait(lua_State *L) {
	double duration = LuaStackOp<double>::check(L, 1);

	WaitTask *task = memnew(WaitTask(L, duration));
	LuauLanguage::get_singleton()->get_task_scheduler().register_task(L, task);

	return lua_yield(L, 0);
}

static int luasched_wait_signal(lua_State *L) {
	Signal signal = LuaStackOp<Signal>::check(L, 1);
	double timeout = luaL_optnumber(L, 2, 10);

	WaitSignalTask *task = memnew(WaitSignalTask(L, signal, timeout));
	LuauLanguage::get_singleton()->get_task_scheduler().register_task(L, task);

	return lua_yield(L, 0);
}

static const luaL_Reg global_funcs[] = {
	{ "wait", luasched_wait },
	{ "wait_signal", luasched_wait_signal },

	{ nullptr, nullptr }
};

void luasched_openlibs(lua_State *L) {
	luaL_register(L, "_G", global_funcs);
}
