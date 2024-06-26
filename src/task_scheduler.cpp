#include "task_scheduler.h"

#include <lua.h>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/array.hpp>
#include <utility>

#include "gd_luau.h"
#include "luagd_lib.h"
#include "luagd_stack.h"
#include "luau_script.h"
#include "wrapped_no_binding.h"

using namespace godot;

///////////
// Tasks //
///////////

ScheduledTask::ScheduledTask(lua_State *L) :
		L(L) {
	// The thread can get collected, even if it is yielded.
	lua_pushthread(L);
	thread_ref = lua_ref(L, -1);
	lua_pop(L, 1); // thread
}

ScheduledTask::~ScheduledTask() {
	lua_unref(L, thread_ref);
}

// wait

bool WaitTask::is_complete() {
	return remaining == 0;
}

int WaitTask::push_results(lua_State *L) {
	double actual_duration = (nb::Time::get_singleton_nb()->get_ticks_usec() - start_time) / 1e6f;
	lua_pushnumber(L, actual_duration);

	return 1;
}

void WaitTask::update(double p_delta) {
	uint64_t delta_usec = p_delta * 1e6;

	if (delta_usec > remaining)
		remaining = 0;
	else
		remaining -= delta_usec;
}

WaitTask::WaitTask(lua_State *L, double p_duration_secs) :
		ScheduledTask(L) {
	duration = p_duration_secs * 1e6;
	remaining = duration;
	start_time = nb::Time::get_singleton_nb()->get_ticks_usec();
}

// wait_signal

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

///////////////
// Scheduler //
///////////////

void TaskScheduler::frame(double p_delta) {
	/* TASKS */
	TaskList::Element *task = tasks.front();

	while (task) {
		Pair<lua_State *, ScheduledTask *> &E = task->get();
		lua_State *L = E.first;
		ScheduledTask *s_task = E.second;

		s_task->update(p_delta);

		LUAU_LOCK(L);

		if (s_task->is_complete()) {
			if (s_task->should_resume()) {
				int results = s_task->push_results(L);

				INIT_TIMEOUT(L)
				int status = lua_resume(L, nullptr, results);

				if (status != LUA_OK && status != LUA_YIELD) {
					GDThreadData *udata = luaGD_getthreaddata(L);

					luaGD_gderror(
							"TaskScheduler::frame",
							udata->script.is_valid() ? udata->script->get_path() : "<unknown>",
							LuaStackOp<String>::get(L, -1));

					lua_pop(L, 1); // error
				}
			}

			// Remove task
			memdelete(s_task);

			TaskList::Element *to_remove = task;
			task = task->next();
			to_remove->erase();
			continue;
		}

		task = task->next();
	}

	/* GC */

	// Update memory usage rates.
	static int32_t new_gcsize[GDLuau::VM_MAX];
	GDLuau::get_singleton()->gc_size(new_gcsize);

	for (int i = 0; i < GDLuau::VM_MAX; i++) {
		gc_rate[i] = (new_gcsize[i] - last_gc_size[i]) / p_delta;
		last_gc_size[i] = new_gcsize[i];
	}

	// Perform GC.
	GDLuau::get_singleton()->gc_step(gc_stepsize, p_delta);

	// Tune step size.
	for (int i = 0; i < GDLuau::VM_MAX; i++) {
		uint32_t curr_size = gc_stepsize[i];
		int32_t rate = gc_rate[i];

		if (rate > curr_size) {
			// Memory usage increasing faster than collection.
			if (curr_size + STEPSIZE_INC > STEPSIZE_MAX) {
				gc_stepsize[i] = STEPSIZE_MAX;
			} else {
				gc_stepsize[i] += STEPSIZE_INC;
			}
		} else if (rate < curr_size) {
			// Memory usage increasing slower than collection.
			if (curr_size < STEPSIZE_MIN + STEPSIZE_INC) {
				gc_stepsize[i] = STEPSIZE_MIN;
			} else {
				gc_stepsize[i] -= STEPSIZE_INC;
			}
		}
	}
}

void TaskScheduler::register_task(lua_State *L, ScheduledTask *p_task) {
	// Push front to avoid iterating over this task if it was created during `frame`
	// (i.e. a thread resumed and yielded again immediately)
	tasks.push_front({ L, p_task });
}
