#pragma once

#include <cstdint>

struct lua_State;
class LuauScript;

class GDLuau {
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
	static GDLuau *singleton;

	lua_State *vms[VM_MAX];
	void init_vm(VMType p_type);

public:
	static GDLuau *get_singleton() { return singleton; }

	lua_State *get_vm(VMType p_type);
	void gc_step(const uint32_t *p_step, double p_delta);
	void gc_size(int32_t *r_buffer);

	GDLuau();
	~GDLuau();
};
