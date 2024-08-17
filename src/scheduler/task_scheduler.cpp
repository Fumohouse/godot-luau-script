#include "scheduler/task_scheduler.h"

#include <lua.h>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "core/lua_utils.h"
#include "core/runtime.h"
#include "core/stack.h"
#include "scripting/luau_script.h"

using namespace godot;

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

void TaskScheduler::frame(double p_delta) {
	/* TASKS */
	TaskList::Element *task = tasks.front();

	while (task) {
		Pair<lua_State *, ScheduledTask *> &E = task->get();
		ThreadHandle L = E.first;
		ScheduledTask *s_task = E.second;

		s_task->update(p_delta);

		if (s_task->is_complete()) {
			if (s_task->should_resume()) {
				int results = s_task->push_results(L);
				int status = luascript_resume(L, nullptr, results);

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
	static int32_t new_gcsize[LuauRuntime::VM_MAX];
	LuauRuntime::get_singleton()->gc_size(new_gcsize);

	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		gc_rate[i] = (new_gcsize[i] - last_gc_size[i]) / p_delta;
		last_gc_size[i] = new_gcsize[i];
	}

	// Perform GC.
	LuauRuntime::get_singleton()->gc_step(gc_stepsize, p_delta);

	// Tune step size.
	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
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
