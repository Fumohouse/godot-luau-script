#pragma once

#include <lua.h>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/classes/ref.hpp>

#include "scheduler/task_scheduler.h"

using namespace godot;

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
