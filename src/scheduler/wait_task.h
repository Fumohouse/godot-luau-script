#pragma once

#include <lua.h>
#include <cstdint>

#include "scheduler/task_scheduler.h"

class WaitTask : public ScheduledTask {
	// usecs
	uint64_t duration;
	uint64_t start_time;
	uint64_t remaining;

public:
	bool is_complete() override;
	int push_results(lua_State *L) override;
	void update(double p_delta) override;

	WaitTask(lua_State *L, double p_duration_secs);
};
