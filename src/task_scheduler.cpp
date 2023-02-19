#include "task_scheduler.h"

#include <lua.h>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "gd_luau.h"
#include "luagd.h"
#include "luagd_stack.h"
#include "luau_script.h"

using namespace godot;

///////////
// Tasks //
///////////

ScheduledTask::ScheduledTask(lua_State *L) {
    // The thread can get collected, even if it is yielded.
    lua_pushthread(L);
    thread_ref = lua_ref(L, -1);
}

bool WaitTask::is_complete() {
    return remaining == 0;
}

int WaitTask::push_results(lua_State *L) {
    double actual_duration = (Time::get_singleton()->get_ticks_usec() - start_time) / 1e6f;
    lua_pushnumber(L, actual_duration);

    return 1;
}

void WaitTask::update(double delta) {
    uint64_t delta_usec = delta * 1e6;

    if (delta_usec > remaining)
        remaining = 0;
    else
        remaining -= delta_usec;
}

WaitTask::WaitTask(lua_State *L, double duration_secs) :
        ScheduledTask(L) {
    duration = duration_secs * 1e6;
    remaining = duration;
    start_time = Time::get_singleton()->get_ticks_usec();
}

///////////////
// Scheduler //
///////////////

void TaskScheduler::frame(double delta) {
    /* TASKS */
    TaskList::Element *task = tasks.front();

    while (task != nullptr) {
        Pair<lua_State *, ScheduledTask *> &pair = task->get();
        pair.second->update(delta);

        lua_State *L = pair.first;
        LUAU_LOCK(L);

        if (pair.second->is_complete()) {
            TaskList::Element *to_remove = task;

            int results = pair.second->push_results(pair.first);
            int status = lua_resume(L, nullptr, results);

            if (status != LUA_OK && status != LUA_YIELD) {
                GDThreadData *udata = luaGD_getthreaddata(L);
                Ref<LuauScript> script = udata->script;
                LUAU_ERR("TaskScheduler::frame", script, 1, LuaStackOp<String>::get(L, -1));

                lua_pop(L, 1); // error
            }

            lua_unref(L, pair.second->get_thread_ref());

            // Remove task
            memdelete(pair.second);
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
        gc_rate[i] = (new_gcsize[i] - last_gc_size[i]) / delta;
        last_gc_size[i] = new_gcsize[i];
    }

    // Perform GC.
    GDLuau::get_singleton()->gc_step(gc_stepsize, delta);

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

        if (gc_stepsize[i] != curr_size)
            UtilityFunctions::print_verbose("task scheduler: vm ", i, ": new gc rate is ", gc_stepsize[i], " KB/s");
    }
}

void TaskScheduler::register_task(lua_State *L, ScheduledTask *task) {
    // Push front to avoid iterating over this task if it was created during `frame`
    // (i.e. a thread resumed and yielded again immediately)
    tasks.push_front({ L, task });
}