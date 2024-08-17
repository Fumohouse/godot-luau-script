#pragma once

#include <cstdint>
#include <godot_cpp/core/mutex_lock.hpp>

using namespace godot;

struct lua_State;

class ThreadHandle {
public:
	ThreadHandle(lua_State *L);
	~ThreadHandle();
	operator lua_State *() const;

private:
	lua_State *L;
};

class LuauRuntime {
public:
	enum VMType {
		// Runs code for getting basic information for LuauScript.
		VM_SCRIPT_LOAD = 0,

		// Runs the core game code.
		VM_CORE,

		// Runs any potentially unsafe user code.
		VM_USER,

		VM_MAX
	};

private:
	static LuauRuntime *singleton;

	lua_State *vms[VM_MAX];
	void init_vm(VMType p_type);

public:
	static LuauRuntime *get_singleton() { return singleton; }

	ThreadHandle get_vm(VMType p_type);
	void gc_step(const uint32_t *p_step, double p_delta);
	void gc_size(int32_t *r_buffer);

	LuauRuntime();
	~LuauRuntime();
};
