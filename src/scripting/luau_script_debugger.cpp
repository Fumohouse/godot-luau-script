#include "scripting/luau_script.h"

#include <lua.h>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "core/lua_utils.h"
#include "core/runtime.h"

LuauLanguage::DebugInfo::StackInfo::operator Dictionary() const {
	Dictionary d;
	d["file"] = source;
	d["func"] = name;
	d["line"] = line;

	return d;
}

void LuauLanguage::debug_init() {
	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		lua_State *L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VMType(i));
		lua_Callbacks *cb = lua_callbacks(L);

		cb->interrupt = LuauLanguage::lua_interrupt;
	}
}

#define STR1(m_x) #m_x
#define STR(m_x) STR1(m_x)

extern void luaG_pusherror(lua_State *L, const char *p_error);

void LuauLanguage::lua_interrupt(lua_State *L, int p_gc) {
	GDThreadData *udata = luaGD_getthreaddata(L);

	if (p_gc < 0 && udata->interrupt_deadline > 0 && (uint64_t)(lua_clock() * 1e6) > udata->interrupt_deadline) {
		lua_checkstack(L, 1);
		luaG_pusherror(L, "thread exceeded maximum execution time (" STR(THREAD_EXECUTION_TIMEOUT) " seconds)");
		lua_error(L);
	}
}

bool LuauLanguage::ar_to_si(lua_Debug &p_ar, DebugInfo::StackInfo &p_si) {
	if (p_ar.source[0] == '@') {
		p_si.source = p_ar.source + 1;
		p_si.name = p_ar.name ? p_ar.name : "unknown function";
		p_si.line = p_ar.currentline;
		return true;
	}

	return false;
}

void LuauLanguage::set_call_stack(lua_State *L) {
	debug.call_lock->lock();
	// Sometimes this is called several times on the same thread.
	debug.call_stack.clear();

	lua_Debug ar;
	// Start at level 1 (level 0 is the C function).
	for (int level = 1; lua_getinfo(L, level, "snl", &ar); level++) {
		DebugInfo::StackInfo si;
		if (ar_to_si(ar, si))
			debug.call_stack.push_back(si);
	}
}

void LuauLanguage::clear_call_stack() {
	debug.call_stack.clear();
	debug.call_lock->unlock();
}

TypedArray<Dictionary> LuauLanguage::_debug_get_current_stack_info() {
	TypedArray<Dictionary> stack_info;

	if (!debug.call_stack.is_empty()) {
		for (const DebugInfo::StackInfo &si : debug.call_stack) {
			stack_info.push_back(si.operator Dictionary());
		}
	}

	return stack_info;
}
