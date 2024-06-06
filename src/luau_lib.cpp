#include "luau_lib.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_bindings.h"
#include "luagd_bindings_stack.gen.h"
#include "luagd_lib.h"
#include "luagd_stack.h"
#include "luau_cache.h"
#include "luau_script.h"
#include "wrapped_no_binding.h"

using namespace godot;

/* STRUCTS */

GDProperty::operator Dictionary() const {
	Dictionary dict;

	dict["type"] = type;
	dict["usage"] = usage;

	dict["name"] = name;
	dict["class_name"] = class_name;

	dict["hint"] = hint;
	dict["hint_string"] = hint_string;

	return dict;
}

GDProperty::operator Variant() const {
	return this->operator Dictionary();
}

GDMethod::operator Dictionary() const {
	Dictionary dict;

	dict["name"] = name;
	dict["return"] = return_val;
	dict["flags"] = flags;

	Array args;
	for (const GDProperty &arg : arguments)
		args.push_back(arg);

	dict["args"] = args;

	Array default_args;
	for (const Variant &default_arg : default_arguments)
		default_args.push_back(default_arg);

	dict["default_args"] = default_args;

	return dict;
}

GDMethod::operator Variant() const {
	return operator Dictionary();
}

GDRpc::operator Dictionary() const {
	Dictionary dict;

	dict["rpc_mode"] = rpc_mode;
	dict["transfer_mode"] = transfer_mode;
	dict["call_local"] = call_local;
	dict["channel"] = channel;

	return dict;
}

GDRpc::operator Variant() const {
	return operator Dictionary();
}

int GDClassDefinition::set_prop(const String &p_name, const GDClassProperty &p_prop) {
	HashMap<StringName, uint64_t>::ConstIterator E = property_indices.find(p_name);

	if (E) {
		properties.set(E->value, p_prop);
		return E->value;
	} else {
		int index = properties.size();
		property_indices[p_name] = index;
		properties.push_back(p_prop);

		return index;
	}
}

/* PROPERTY */

GDProperty luascript_read_property(lua_State *L, int p_idx) {
	if (!lua_istable(L, p_idx))
		luaL_error(L, "expected table type for GDProperty value");

	GDProperty property;

	if (luaGD_getfield(L, p_idx, "type"))
		property.type = static_cast<GDExtensionVariantType>(luaGD_checkvaluetype<uint32_t>(L, -1, "type", LUA_TNUMBER));

	if (luaGD_getfield(L, p_idx, "name"))
		property.name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

	if (luaGD_getfield(L, p_idx, "hint"))
		property.hint = static_cast<PropertyHint>(luaGD_checkvaluetype<uint32_t>(L, -1, "hint", LUA_TNUMBER));

	if (luaGD_getfield(L, p_idx, "hintString"))
		property.hint_string = luaGD_checkvaluetype<String>(L, -1, "hintString", LUA_TSTRING);

	if (luaGD_getfield(L, p_idx, "usage"))
		property.usage = luaGD_checkvaluetype<uint32_t>(L, -1, "usage", LUA_TNUMBER);

	if (luaGD_getfield(L, p_idx, "className"))
		property.class_name = luaGD_checkvaluetype<String>(L, -1, "className", LUA_TSTRING);

	return property;
}

/* CLASS */

static int luascript_gdclass_new(lua_State *L) {
	LuauScript *script = luaGD_lightudataup<LuauScript>(L, 1);

	if (script->is_loading())
		luaL_error(L, "cannot instantiate: script is loading");

	StringName class_name = script->_get_instance_base_type();

	GDExtensionObjectPtr ptr = internal::gdextension_interface_classdb_construct_object(&class_name);
	nb::Object(ptr).set_script(Ref<LuauScript>(script));

	LuaStackOp<Object *>::push(L, ptr);
	return 1;
}

static int luascript_gdclass(lua_State *L) {
	GDThreadData *udata = luaGD_getthreaddata(L);
	if (udata->script.is_null())
		luaL_error(L, "this function can only be run from a script thread");

	luaL_checktype(L, 1, LUA_TTABLE);

	if (lua_getmetatable(L, 1))
		luaL_error(L, "custom metatables are not supported on class definitions");

	lua_createtable(L, 0, 3);

	lua_pushstring(L, "This metatable is locked.");
	lua_setfield(L, -2, "__metatable");

	LuaStackOp<Object *>::push(L, udata->script.ptr());
	lua_setfield(L, -2, LUASCRIPT_MT_SCRIPT);

	{
		lua_createtable(L, 0, 1);

		lua_pushlightuserdata(L, udata->script.ptr());
		lua_pushcclosure(L, luascript_gdclass_new, "GDClass.new", 1);
		lua_setfield(L, -2, "new");

		lua_setreadonly(L, -1, true);
		lua_setfield(L, -2, "__index");
	}

	lua_setreadonly(L, -1, true);
	lua_setmetatable(L, -2);

	return 1;
}

LuauScript *luascript_class_table_get_script(lua_State *L, int p_i) {
	if (!lua_istable(L, p_i))
		return nullptr;

	if (!lua_getmetatable(L, p_i))
		return nullptr;

	lua_getfield(L, -1, LUASCRIPT_MT_SCRIPT);

	if (!lua_isnil(L, -1) && LuaStackOp<Object *>::is(L, -1)) {
		Object *obj = ObjectDB::get_instance(*LuaStackOp<Object *>::get_id(L, -1));
		lua_pop(L, 2); // metatable, value
		return Object::cast_to<LuauScript>(obj);
	}

	lua_pop(L, 2); // metatable, value
	return nullptr;
}

/* LIBRARY */

// Based on Luau Repl implementation.
static int finishrequire(lua_State *L) {
	if (lua_isstring(L, -1))
		// Avoid lua_error to make sure line numbers are printed correctly
		luaL_error(L, "%s", lua_tostring(L, -1));

	return 1;
}

static int luascript_require(lua_State *L) {
	GDThreadData *udata = luaGD_getthreaddata(L);
	if (udata->script.is_null())
		luaL_error(L, "this function can only be run from a script thread");

	String path = LuaStackOp<String>::check(L, 1);

	luaL_findtable(L, LUA_REGISTRYINDEX, LUASCRIPT_MODULE_TABLE, 1);

	// Get full path.
	String path_err;
	String full_path = udata->script->resolve_path(path, path_err);
	if (!path_err.is_empty()) {
		luaL_error(L, "require failed: %s", path_err.utf8().get_data());
	}

	// Checks.
	if (udata->script->get_path() == full_path)
		luaL_error(L, "scripts cannot require themselves");

	if (FileAccess::file_exists(full_path + ".lua")) {
		full_path = full_path + ".lua";
	} else {
		luaL_error(L, "could not find module: %s", path.utf8().get_data());
	}

	CharString full_path_utf8 = full_path.utf8();

	// Load and write dependency.
	Error err = OK;
	// Do not use a Ref here!
	// luaL_error, etc. will not properly free Refs and will cause memory leaks and crashes!
	LuauScript *script = LuauCache::get_singleton()->get_script(full_path, err).ptr();

	// Dependencies are, for the most part, not of any concern if they aren't part of the load stage.
	// (i.e. requires processed after initial script load should not write or check dependencies)
	if (udata->script->is_loading() && !udata->script->add_dependency(script)) {
		luaL_error(L, "cyclic dependency detected in %s. halting require of %s.",
				udata->script->get_path().utf8().get_data(),
				full_path_utf8.get_data());
	}

	// Return from cache.
	lua_getfield(L, -1, full_path_utf8.get_data());
	if (!lua_isnil(L, -1)) {
		return finishrequire(L);
	}

	lua_pop(L, 1);

	// Load module and return.
	if (script->is_module()) {
		script->load_module(L);
		lua_remove(L, -2); // thread
	} else {
		if (udata->vm_type >= GDLuau::VM_MAX)
			luaL_error(L, "could not get class definition: unknown VM");

		if (script->load_table(udata->vm_type) == OK) {
			lua_getref(L, script->get_table_ref(udata->vm_type));
		} else {
			LuaStackOp<String>::push(L, "could not get class definition for script at " + script->get_path());
		}
	}

	lua_pushvalue(L, -1);
	lua_setfield(L, -3, full_path_utf8.get_data());

	return finishrequire(L);
}

static int luascript_wait(lua_State *L) {
	double duration = LuaStackOp<double>::check(L, 1);

	WaitTask *task = memnew(WaitTask(L, duration));
	LuauLanguage::get_singleton()->get_task_scheduler().register_task(L, task);

	return lua_yield(L, 0);
}

static int luascript_wait_signal(lua_State *L) {
	Signal signal = LuaStackOp<Signal>::check(L, 1);
	double timeout = luaL_optnumber(L, 2, 10);

	WaitSignalTask *task = memnew(WaitSignalTask(L, signal, timeout));
	LuauLanguage::get_singleton()->get_task_scheduler().register_task(L, task);

	return lua_yield(L, 0);
}

static int luascript_gdglobal(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);

	HashMap<StringName, Variant>::ConstIterator E = LuauLanguage::get_singleton()->get_global_constants().find(name);

	if (E) {
		LuaStackOp<Variant>::push(L, E->value);
		return 1;
	}

	luaL_error(L, "singleton '%s' was not found", name);
}

static const luaL_Reg global_funcs[] = {
	{ "gdclass", luascript_gdclass },

	{ "require", luascript_require },

	{ "wait", luascript_wait },
	{ "wait_signal", luascript_wait_signal },

	{ "gdglobal", luascript_gdglobal },

	{ nullptr, nullptr }
};

/* EXPOSED FUNCTIONS */

void luascript_get_classdef_or_type(lua_State *L, int p_index, String &r_type, LuauScript *&r_script) {
#define CLASS_GLOBAL_EXPECTED_ERR "expected a class global (i.e. metatable containing the " MT_CLASS_GLOBAL " field)"

	if (lua_isstring(L, p_index)) {
		r_type = lua_tostring(L, p_index);
	} else if (LuauScript *script = luascript_class_table_get_script(L, p_index)) {
		r_script = script;
	} else if (lua_istable(L, p_index)) {
		if (!lua_getmetatable(L, p_index))
			luaL_error(L, CLASS_GLOBAL_EXPECTED_ERR);

		lua_getfield(L, -1, MT_CLASS_GLOBAL);
		if (lua_type(L, -1) != LUA_TSTRING)
			luaL_error(L, CLASS_GLOBAL_EXPECTED_ERR);

		r_type = lua_tostring(L, -1);

		lua_pop(L, 2); // value, metatable
	} else {
		luaL_typeerrorL(L, p_index, "string, GDClassDefinition, or ClassGlobal");
	}
}

String luascript_get_scriptname_or_type(lua_State *L, int p_index, LuauScript **r_script) {
	String type;
	LuauScript *script = nullptr;
	luascript_get_classdef_or_type(L, p_index, type, script);

	if (script) {
		type = script->get_definition().name;
		if (type.is_empty())
			luaL_error(L, "could not determine script name from script at %s; did you name it?", script->get_path().utf8().get_data());

		if (r_script)
			*r_script = script;
	}

	return type;
}

void luascript_openlibs(lua_State *L) {
	luaL_register(L, "_G", global_funcs);
}
