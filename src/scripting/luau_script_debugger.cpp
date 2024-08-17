#include "scripting/luau_script.h"

#include <lua.h>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "core/lua_utils.h"
#include "core/runtime.h"
#include "scripting/luau_lib.h"
#include "utils/wrapped_no_binding.h"

#ifdef TOOLS_ENABLED
void LuauScript::ref_thread(lua_State *L) {
	GDThreadData *udata = luaGD_getthreaddata(L);
	LuauRuntime::VMType vm_type = udata->vm_type;

	if (vm_type != LuauRuntime::VM_MAX) {
		if (debug.function_refs[vm_type]) {
			lua_unref(L, debug.function_refs[vm_type]);
		}

		debug.function_refs[vm_type] = lua_ref(L, -1);

		for (const int &line : debug.breakpoints) {
			lua_breakpoint(L, -1, line, true);
		}
	}
}

void LuauScript::set_breakpoint(int p_line, bool p_enabled) {
	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		int ref = debug.function_refs[i];
		if (!ref)
			continue;

		ThreadHandle L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VMType(i));
		lua_getref(L, ref);
		lua_breakpoint(L, -1, p_line, p_enabled);

		lua_pop(L, 1);
	}
}

void LuauScript::insert_breakpoint(int p_line) {
	if (debug.breakpoints.has(p_line))
		return;

	set_breakpoint(p_line, true);
	debug.breakpoints.insert(p_line);
}

void LuauScript::remove_breakpoint(int p_line) {
	if (!debug.breakpoints.has(p_line))
		return;

	set_breakpoint(p_line, false);
	debug.breakpoints.erase(p_line);
}

LuauLanguage::DebugInfo::StackInfo::operator Dictionary() const {
	Dictionary d;
	d["file"] = source;
	d["func"] = name;
	d["line"] = line;

	return d;
}

void LuauLanguage::debug_init() {
	for (int i = 0; i < LuauRuntime::VM_MAX; i++) {
		ThreadHandle L = LuauRuntime::get_singleton()->get_vm(LuauRuntime::VMType(i));
		lua_Callbacks *cb = lua_callbacks(L);

		cb->interrupt = LuauLanguage::lua_interrupt;
		cb->debuginterrupt = LuauLanguage::lua_debuginterrupt;
		cb->debugbreak = LuauLanguage::lua_debugbreak;
		cb->debugstep = LuauLanguage::lua_debugstep;
		cb->debugprotectederror = LuauLanguage::lua_debugprotectederror;
	}
}

void LuauLanguage::debug_reset() {
	if (!debug.L)
		return;

	lua_singlestep(debug.L, false);
	lua_unref(debug.L, debug.thread_ref);

	debug.break_call_stack.clear();

	debug.thread_ref = 0;
	debug.L = nullptr;
	debug.error = "";
	debug.break_depth = -1;

	debug.base_break_source = "";
	debug.base_break_line = -1;
}

#define STR1(m_x) #m_x
#define STR(m_x) STR1(m_x)

extern void luaG_pusherror(lua_State *L, const char *p_error);

void LuauLanguage::lua_interrupt(lua_State *L, int p_gc) {
	nb::EngineDebugger::get_singleton_nb()->line_poll();

	GDThreadData *udata = luaGD_getthreaddata(L);

	if (udata->interrupt_deadline == 0)
		return;

	// Prevent interrupt due to debug break
	if (udata->interrupt_deadline < get_singleton()->debug.interrupt_reset) {
		udata->interrupt_deadline = (lua_clock() + THREAD_EXECUTION_TIMEOUT) * 1e6;
		return;
	}

	if (p_gc < 0 && (uint64_t)(lua_clock() * 1e6) > udata->interrupt_deadline) {
		lua_checkstack(L, 1);
		luaG_pusherror(L, "thread exceeded maximum execution time (" STR(THREAD_EXECUTION_TIMEOUT) " seconds)");
		lua_error(L);
	}
}

void LuauLanguage::lua_debuginterrupt(lua_State *L, lua_Debug *ar) {
	lua_State *coroutine = reinterpret_cast<lua_State *>(ar->userdata);

	int state = LUA_BREAK;
	while (state == LUA_BREAK) {
		state = lua_resume(coroutine, nullptr, 0);
	}
}

void LuauLanguage::lua_debugbreak(lua_State *L, lua_Debug *ar) {
	GDThreadData *udata = luaGD_getthreaddata(L);
	if (udata->vm_type == LuauRuntime::VM_MAX)
		return;

	int &breakhits = get_singleton()->debug.breakhits[udata->vm_type];
	breakhits++;

	// Each breakpoint will be hit once on initial invocation and again on
	// resume. Ignore the second one to avoid infinite breaking.
	if (breakhits % 2 == 1)
		get_singleton()->debug_break(L);
}

void LuauLanguage::lua_debugstep(lua_State *L, lua_Debug *ar) {
	nb::EngineDebugger *debugger = nb::EngineDebugger::get_singleton_nb();

	DebugInfo &dbg = LuauLanguage::get_singleton()->debug;
	if (dbg.L != L || debugger->get_lines_left() <= 0)
		return;

	int curr_depth = lua_stackdepth(L);

	if (debugger->get_depth() >= 0) {
		debugger->set_depth(curr_depth - dbg.break_depth);
	}

	if (debugger->get_depth() <= 0 &&
			// Since singlestep works based on instructions and not lines,
			// detect that either depth has changed or the line has changed.
			(curr_depth != dbg.break_depth || ar->currentline != dbg.break_call_stack[0].line)) {
		debugger->set_lines_left(debugger->get_lines_left() - 1);
	}

	if (debugger->get_lines_left() <= 0) {
		lua_singlestep(L, false);
		get_singleton()->debug_break(L, true);
	}
}

void LuauLanguage::lua_debugprotectederror(lua_State *L) {
	get_singleton()->debug_break(L);
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

static Variant get_lua_val(lua_State *L, int idx) {
	lua_checkstack(L, 2); // for ::is, ::get
	if (!lua_istable(L, idx) && LuaStackOp<Variant>::is(L, idx)) {
		return LuaStackOp<Variant>::get(L, idx);
	}

	Array vals;
	vals.push_back(luaL_typename(L, idx));
	vals.push_back((uint64_t)lua_topointer(L, idx));
	return String("[lua {0}@{1}]").format(vals);
}

void LuauLanguage::debug_break(const ThreadHandle &L, bool p_is_step) {
	static bool debugging = false;
	ERR_FAIL_COND_MSG(debugging, "could not break: already debugging another thread");

	// Luau may be in an errored state. Before anything is added to the stack,
	// special care to run lua_checkstack must be taken.

	if (!debug.L || debug.L != L) {
		debug_reset();

		lua_checkstack(L, 1);
		lua_pushthread(L);
		debug.thread_ref = lua_ref(L, -1);
		lua_pop(L, 1);

		debug.L = L;
	}

	int status = lua_status(L);

	switch (status) {
		// These errors match ldebug.h
		case LUA_ERRMEM:
			debug.error = "not enough memory";
			break;

		case LUA_ERRERR:
			debug.error = "error in error handling";
			break;

		case LUA_ERRSYNTAX:
		case LUA_ERRRUN:
			debug.error = lua_tostring(L, -1);
			break;

		default:
			debug.error = "Breakpoint";
			break;
	}

	debug.break_call_stack.clear();

	bool base_found = false;

	lua_Debug ar;
	for (int level = 0; lua_getinfo(L, level, "snl", &ar); level++) {
		DebugInfo::BreakStackInfo si;
		if (!ar_to_si(ar, si))
			continue;

		if (!p_is_step && level == 0) {
			debug.base_break_source = ar.source;
			debug.base_break_line = ar.linedefined;
		}

		if (!base_found && p_is_step &&
				ar.source == debug.base_break_source && ar.linedefined == debug.base_break_line) {
			base_found = true;
		}

		// Locals
		for (int local = 1; const char *name = (lua_checkstack(L, 1), lua_getlocal(L, level, local)); local++) {
			si.locals.insert(name, get_lua_val(L, -1));
			lua_pop(L, 1);
		}

		// Members
		HashMap<String, Variant>::ConstIterator E = si.locals.find("self");
		if (E && E->value.get_type() == Variant::OBJECT) {
			uint64_t id = E->value.operator ObjectID();
			GDExtensionObjectPtr objPtr = internal::gdextension_interface_object_get_instance_from_id(id);
			nb::Object obj(objPtr);

			TypedArray<Dictionary> props = obj.get_property_list();
			for (int i = 0; i < props.size(); i++) {
				const Dictionary &prop = props[i];

				int type = prop["type"];
				int usage = prop["usage"];
				if (type == Variant::NIL && !(usage & PROPERTY_USAGE_NIL_IS_VARIANT))
					continue;

				const String &prop_name = prop["name"];
				si.members.insert(prop_name, obj.get(prop_name));
			}

			if (LuauScriptInstance *inst = LuauScriptInstance::from_object(objPtr)) {
				lua_checkstack(L, 1);
				if (inst->get_table(L)) {
					lua_checkstack(L, 1);
					lua_pushnil(L);
					while ((lua_checkstack(L, 2), lua_next(L, -2)) != 0) {
						String key = LuaStackOp<String>::get(L, -2);
						if (!si.members.has(key)) {
							si.members.insert(key, get_lua_val(L, -1));
						}

						lua_pop(L, 1); // value
					}

					lua_pop(L, 1); // table
				}

				HashMap<LuauScriptInstance *, void *>::ConstIterator F = instance_to_godot.find(inst);
				if (F) {
					si.instance = F->value;
				}
			}
		}

		debug.break_call_stack.push_back(si);
	}

	debug.break_depth = lua_stackdepth(L);

	if (p_is_step && !base_found) {
		// Avoid stepping outside of the original breakpoint context
		return;
	}

	debugging = true;

	// Since singlestep cannot be updated in the middle of execution, break and
	// restart. Any hooks need to return before resuming, so handle elsewhere
	// (see luascript_resume, luascript_pcall)
	lua_break(L);

	bool is_err = status != LUA_OK && status != LUA_YIELD;
	nb::EngineDebugger::get_singleton_nb()->script_debug(this, !is_err, is_err);
	debug.interrupt_reset = (lua_clock() + THREAD_EXECUTION_TIMEOUT) * 1e6;

	if (nb::EngineDebugger::get_singleton_nb()->get_lines_left() > 0) {
		lua_singlestep(L, true);
	}

	debugging = false;
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
#endif // TOOLS_ENABLED

String LuauLanguage::_debug_get_error() const {
#ifdef TOOLS_ENABLED
	return debug.error;
#else
	return "";
#endif // TOOLS_ENABLED
}

int32_t LuauLanguage::_debug_get_stack_level_count() const {
#ifdef TOOLS_ENABLED
	return debug.break_call_stack.size();
#else
	return 0;
#endif // TOOLS_ENABLED
}

int32_t LuauLanguage::_debug_get_stack_level_line(int32_t p_level) const {
#ifdef TOOLS_ENABLED
	return debug.break_call_stack[p_level].line;
#else
	return 0;
#endif // TOOLS_ENABLED
}

String LuauLanguage::_debug_get_stack_level_function(int32_t p_level) const {
#ifdef TOOLS_ENABLED
	return debug.break_call_stack[p_level].name;
#else
	return "";
#endif // TOOLS_ENABLED
}

String LuauLanguage::_debug_get_stack_level_source(int32_t p_level) const {
#ifdef TOOLS_ENABLED
	return debug.break_call_stack[p_level].source;
#else
	return "";
#endif // TOOLS_ENABLED
}

Dictionary LuauLanguage::_debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;

#ifdef TOOLS_ENABLED
	PackedStringArray locals;
	Array values;
	for (const KeyValue<String, Variant> &E : debug.break_call_stack[p_level].locals) {
		locals.push_back(E.key);
		values.push_back(E.value);
	}

	d["locals"] = locals;
	d["values"] = values;
#endif // TOOLS_ENABLED

	return d;
}

Dictionary LuauLanguage::_debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;

#ifdef TOOLS_ENABLED
	PackedStringArray members;
	Array values;
	for (const KeyValue<String, Variant> &E : debug.break_call_stack[p_level].members) {
		members.push_back(E.key);
		values.push_back(E.value);
	}

	d["members"] = members;
	d["values"] = values;
#endif // TOOLS_ENABLED

	return d;
}

void *LuauLanguage::_debug_get_stack_level_instance(int32_t p_level) {
#ifdef TOOLS_ENABLED
	return debug.break_call_stack[p_level].instance;
#else
	return nullptr;
#endif // TOOLS_ENABLED
}

String LuauLanguage::_debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) {
	// TODO: Debugger `print` command
	return "";
}

TypedArray<Dictionary> LuauLanguage::_debug_get_current_stack_info() {
	TypedArray<Dictionary> stack_info;

#ifdef TOOLS_ENABLED
	if (!debug.call_stack.is_empty()) {
		for (const DebugInfo::StackInfo &si : debug.call_stack) {
			stack_info.push_back(si.operator Dictionary());
		}
	}
#endif // TOOLS_ENABLED

	return stack_info;
}
