#pragma once

#include <cstdint>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/signal.hpp>

#include "gd_luau.h"

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

class SignalWaiter : public RefCounted {
	GDCLASS(SignalWaiter, RefCounted)

	lua_State *L;
	Signal signal;
	Callable callable;

	bool _got_signal = false;

protected:
	static void _bind_methods();

public:
	void initialize(lua_State *L, Signal p_signal);
	void on_signal(const Variant **p_args, GDExtensionInt p_argc, GDExtensionCallError &r_err);
	bool got_signal() const { return _got_signal; }

	SignalWaiter() :
			callable(Callable(this, "on_signal")) {}
};

class WaitSignalTask : public ScheduledTask {
	Ref<SignalWaiter> waiter;
	uint64_t until_timeout; // usec

public:
	bool is_complete() override;
	bool should_resume() override;
	int push_results(lua_State *L) override;
	void update(double delta) override;

	WaitSignalTask(lua_State *L, Signal p_signal, double p_timeout_secs);
};

typedef List<Pair<lua_State *, ScheduledTask *>> TaskList;

class TaskScheduler {
	TaskList tasks;

	uint32_t gc_stepsize[GDLuau::VM_MAX] = { STEPSIZE_MIN };

	int32_t last_gc_size[GDLuau::VM_MAX] = { 0 };
	int32_t gc_rate[GDLuau::VM_MAX] = { 0 };

public:
	void frame(double p_delta);
	void register_task(lua_State *L, ScheduledTask *p_task);

	const uint32_t *get_gc_step_size() const { return gc_stepsize; }
};
