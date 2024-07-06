#pragma once

#include <cstdint>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>

#include "core/runtime.h"

using namespace godot;

struct lua_State;

// Luau uses 1 "step unit" ~= 1KB
// amount (bytes) = step << 10
#define STEPSIZE_MIN 50
#define STEPSIZE_INC 25
#define STEPSIZE_MAX 10000

class ScheduledTask {
	int thread_ref;

protected:
	lua_State *L;

public:
	int get_thread_ref() { return thread_ref; }

	virtual bool is_complete() = 0;
	virtual bool should_resume() { return true; }
	virtual int push_results(lua_State *L) = 0;
	virtual void update(double p_delta) = 0;

	ScheduledTask(lua_State *L);
	virtual ~ScheduledTask();
};

typedef List<Pair<lua_State *, ScheduledTask *>> TaskList;

class TaskScheduler {
	TaskList tasks;

	uint32_t gc_stepsize[LuauRuntime::VM_MAX] = { STEPSIZE_MIN };

	int32_t last_gc_size[LuauRuntime::VM_MAX] = { 0 };
	int32_t gc_rate[LuauRuntime::VM_MAX] = { 0 };

public:
	void frame(double p_delta);
	void register_task(lua_State *L, ScheduledTask *p_task);

	const uint32_t *get_gc_step_size() const { return gc_stepsize; }
};
