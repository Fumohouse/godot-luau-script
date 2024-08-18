#include "core/runtime.h"

#include <Luau/CodeGen.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/mutex.hpp>

#include "core/lua_utils.h"
#include "core/permissions.h"
#include "scheduler/scheduler_lib.h"
#include "scripting/luau_lib.h"
#include "services/luau_interface.h"

using namespace godot;

ThreadHandle::ThreadHandle(lua_State *L) :
		L(L) {
	if (Mutex *mut = luaGD_getthreaddata(L)->lock.ptr())
		mut->lock();
}

ThreadHandle::~ThreadHandle() {
	if (Mutex *mut = luaGD_getthreaddata(L)->lock.ptr())
		mut->unlock();
}

ThreadHandle::operator lua_State *() const { return L; }

LuauRuntime *LuauRuntime::singleton = nullptr;

void LuauRuntime::init_vm(VMType p_type) {
	lua_State *L = luaGD_newstate(p_type, PERMISSION_BASE);
	luascript_openlibs(L);
	luasched_openlibs(L);

	if (LuauInterface::get_singleton()) {
		LuauInterface::get_singleton()->register_metatables(L);
		LuauInterface::get_singleton()->lua_push(L);
		lua_setglobal(L, LuauInterface::get_singleton()->get_name());
	}

	// Seal main global state
	luaL_sandbox(L);

	if (Luau::CodeGen::isSupported())
		Luau::CodeGen::create(L);

	vms[p_type] = L;
}

LuauRuntime::LuauRuntime() {
	init_vm(VM_SCRIPT_LOAD);
	init_vm(VM_CORE);
	init_vm(VM_USER);

	if (!singleton)
		singleton = this;
}

LuauRuntime::~LuauRuntime() {
	if (singleton == this)
		singleton = nullptr;

	for (lua_State *&L : vms) {
		luaGD_close(L);
		L = nullptr;
	}
}

ThreadHandle LuauRuntime::get_vm(VMType p_type) {
	return vms[p_type];
}

void LuauRuntime::gc_step(const uint32_t *p_step, double p_delta) {
	for (int i = 0; i < VM_MAX; i++) {
		ThreadHandle L = get_vm(VMType(i));
		lua_gc(L, LUA_GCSTEP, p_step[i] * p_delta);
	}
}

void LuauRuntime::gc_size(int32_t *r_buffer) {
	for (int i = 0; i < VM_MAX; i++) {
		ThreadHandle L = get_vm(VMType(i));
		r_buffer[i] = lua_gc(L, LUA_GCCOUNT, 0);
	}
}
