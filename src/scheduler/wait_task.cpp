#include "scheduler/wait_task.h"

#include <lua.h>

#include "utils/wrapped_no_binding.h"

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
