#include "debug_service.h"

#include <Luau/Compiler.h>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <string>

#include "core/lua_utils.h"
#include "core/permissions.h"
#include "core/runtime.h"
#include "lua.h"
#include "scripting/luau_script.h"
#include "services/class_binder.h"
#include "services/luau_interface.h"

#define DEBUG_SERVICE_NAME "DebugService"
#define DEBUG_SERVICE_MT_NAME "Luau." DEBUG_SERVICE_NAME
SVC_STACK_OP_IMPL(DebugService, DEBUG_SERVICE_MT_NAME)

DebugService *DebugService::singleton = nullptr;

const LuaGDClass &DebugService::get_lua_class() const {
	static LuaGDClass lua_class;
	static bool did_init = false;

	if (!did_init) {
		lua_class.set_name(DEBUG_SERVICE_NAME, DEBUG_SERVICE_MT_NAME);

		lua_class.bind_method("GCCount", FID(&DebugService::gc_count), PERMISSION_INTERNAL);
		lua_class.bind_method("GCStepSize", FID(&DebugService::gc_step_size), PERMISSION_INTERNAL);
		lua_class.bind_method("Exec", FID(&DebugService::exec), PERMISSION_INTERNAL);

		did_init = true;
	}

	return lua_class;
}

void DebugService::lua_push(lua_State *L) {
	LuaStackOp<DebugService *>::push(L, this);
}

PackedFloat64Array DebugService::gc_count() const {
	PackedFloat64Array arr;
	arr.resize(LuauRuntime::VM_MAX);

	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		lua_State *L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VMType(i));
		double mem_kb = lua_gc(L, LUA_GCCOUNT, 0) + lua_gc(L, LUA_GCCOUNTB, 0) / 1024.0;

		arr[i] = mem_kb;
	}

	return arr;
}

PackedInt32Array DebugService::gc_step_size() const {
	PackedInt32Array arr;
	arr.resize(LuauRuntime::VM_MAX);

	const uint32_t *rates = LuauLanguage::get_singleton()->get_task_scheduler().get_gc_step_size();

	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		arr[i] = rates[i];
	}

	return arr;
}

String DebugService::exec(const String &p_src) {
	if (!T) {
		lua_State *L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VM_CORE);
		LUAU_LOCK(L);
		T = lua_newthread(L);
		thread_ref = lua_ref(L, -1);

		luaGD_getthreaddata(T)->permissions = PERMISSIONS_ALL; // with great power...

		lua_pop(L, 1); // thread
	}

	LUAU_LOCK(T);

	// Recover
	int status = lua_status(T);
	if (status == LUA_YIELD) {
		return "thread is currently yielded";
	} else if (status != LUA_OK) {
		lua_resetthread(T);
	}

	std::string bytecode = Luau::compile(p_src.utf8().get_data(), luaGD_compileopts());

	if (luau_load(T, "=exec", bytecode.data(), bytecode.size(), 0) == 0) {
		INIT_TIMEOUT(T);
		int status = lua_resume(T, nullptr, 0);

		if (status == LUA_OK || status == LUA_YIELD) {
			return "";
		}
	}

	String err = LuaStackOp<String>::get(T, -1);
	return err;
}

DebugService::DebugService() {
	if (!singleton) {
		singleton = this;
	}
}

DebugService::~DebugService() {
	if (singleton == this) {
		singleton = nullptr;
	}

	// Not a normal condition. LuauRuntime is deinitialized before LuauInterface
	if (T && LuauRuntime::get_singleton()) {
		lua_State *L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VM_CORE);
		lua_unref(L, thread_ref);

		T = nullptr;
		thread_ref = 0;
	}
}
