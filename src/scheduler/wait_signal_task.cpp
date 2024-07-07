#include "scheduler/wait_signal_task.h"

#include <lua.h>
#include <godot_cpp/variant/signal.hpp>

#include "core/lua_utils.h"
#include "scripting/luau_script.h"

void SignalWaiter::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "on_signal", &SignalWaiter::on_signal);
}

void SignalWaiter::initialize(lua_State *L, Signal p_signal) {
	this->L = L;
	signal = std::move(p_signal);

	signal.connect(callable);
}

void SignalWaiter::on_signal(const Variant **p_args, GDExtensionInt p_argc, GDExtensionCallError &r_err) {
	{
		LUAU_LOCK(L);

		lua_pushboolean(L, true);

		for (int i = 0; i < p_argc; i++) {
			LuaStackOp<Variant>::push(L, *p_args[i]);
		}

		INIT_TIMEOUT(L)
		int status = lua_resume(L, nullptr, p_argc + 1);

		if (status != LUA_OK && status != LUA_YIELD) {
			GDThreadData *udata = luaGD_getthreaddata(L);
			udata->script->error("SignalWaiter::on_signal", LuaStackOp<String>::get(L, -1));

			lua_pop(L, 1); // error
		}
	}

	signal.disconnect(callable);
	_got_signal = true;
}

bool WaitSignalTask::is_complete() {
	return until_timeout == 0 || waiter->got_signal();
}

bool WaitSignalTask::should_resume() {
	return until_timeout == 0 && !waiter->got_signal();
}

int WaitSignalTask::push_results(lua_State *L) {
	lua_pushboolean(L, false);
	return 1;
}

void WaitSignalTask::update(double p_delta) {
	uint64_t delta_usec = p_delta * 1e6;

	if (until_timeout > delta_usec) {
		until_timeout -= delta_usec;
	} else {
		until_timeout = 0;
	}
}

WaitSignalTask::WaitSignalTask(lua_State *L, Signal p_signal, double p_timeout_secs) :
		ScheduledTask(L) {
	until_timeout = p_timeout_secs * 1e6;

	waiter.instantiate();
	waiter->initialize(L, std::move(p_signal));
}
