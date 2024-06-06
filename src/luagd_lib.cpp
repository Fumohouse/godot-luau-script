#include "luagd_lib.h"

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "extension_api.h"
#include "gd_luau.h"
#include "luagd_bindings.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "services/sandbox_service.h"
#include "wrapped_no_binding.h"

using namespace godot;

// Based on the default implementation seen in the Lua 5.1 reference
static void *luaGD_alloc(void *, void *p_ptr, size_t, size_t p_nsize) {
	if (p_nsize == 0) {
		// Lua assumes free(NULL) is ok. For Godot it is not.
		if (p_ptr)
			memfree(p_ptr);

		return nullptr;
	}

	return memrealloc(p_ptr, p_nsize);
}

static GDThreadData *luaGD_initthreaddata(lua_State *LP, lua_State *L) {
	GDThreadData *udata = memnew(GDThreadData);
	lua_setthreaddata(L, udata);

	if (LP) {
		GDThreadData *parent_udata = luaGD_getthreaddata(LP);
		udata->vm_type = parent_udata->vm_type;
		udata->permissions = parent_udata->permissions;
		udata->script = parent_udata->script;
		udata->lock = parent_udata->lock;
	}

	return udata;
}

static void luaGD_userthread(lua_State *LP, lua_State *L) {
	if (LP) {
		luaGD_initthreaddata(LP, L);
	} else {
		GDThreadData *udata = luaGD_getthreaddata(L);
		if (udata) {
			lua_setthreaddata(L, nullptr);
			memdelete(udata);
		}
	}
}

lua_State *luaGD_newstate(GDLuau::VMType p_vm_type, BitField<ThreadPermissions> p_base_permissions) {
	lua_State *L = lua_newstate(luaGD_alloc, nullptr);

	luaL_openlibs(L);
	luaGD_openlibs(L);
	luaGD_openbuiltins(L);
	luaGD_openclasses(L);
	luaGD_openglobals(L);

	GDThreadData *udata = luaGD_initthreaddata(nullptr, L);
	udata->vm_type = p_vm_type;
	udata->permissions = p_base_permissions;
	udata->lock.instantiate();

	lua_Callbacks *callbacks = lua_callbacks(L);
	callbacks->userthread = luaGD_userthread;

	return L;
}

lua_State *luaGD_newthread(lua_State *L, BitField<ThreadPermissions> p_permissions) {
	lua_State *T = lua_newthread(L);

	GDThreadData *udata = luaGD_getthreaddata(T);
	udata->permissions = p_permissions;

	return T;
}

GDThreadData *luaGD_getthreaddata(lua_State *L) {
	return reinterpret_cast<GDThreadData *>(lua_getthreaddata(L));
}

void luaGD_close(lua_State *L) {
	L = lua_mainthread(L);

	GDThreadData *udata = luaGD_getthreaddata(L);
	if (udata) {
		lua_setthreaddata(L, nullptr);
		memdelete(udata);
	}

	lua_close(L);
}

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key) {
	lua_getfield(L, p_index, p_key);

	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	return true;
}

/* GLOBAL */

template <typename T>
static int luaGD_str_ctor(lua_State *L) {
	String str = LuaStackOp<String>::check(L, 1);
	LuaStackOp<T>::push(L, str, true);
	return 1;
}

static int luaGD_i64_ctor(lua_State *L) {
	int64_t value = 0;

	if (lua_type(L, 1) == LUA_TSTRING) {
		String s = LuaStackOp<String>::get(L, 1);
		if (!s.is_valid_int())
			luaL_typeerrorL(L, 1, "integer");

		value = s.to_int();
	} else {
		value = luaL_checkinteger(L, 1);
	}

	LuaStackOp<int64_t>::push_i64(L, value);
	return 1;
}

static int luaGD_load(lua_State *L) {
	String path = LuaStackOp<String>::check(L, 1);
	GDThreadData *udata = luaGD_getthreaddata(L);

	if (!path.begins_with("res://") && !path.begins_with("user://")) {
		path = udata->script->get_path().get_base_dir().path_join(path);
	}

	if (SandboxService::get_singleton() &&
			udata->script.is_valid() &&
			!SandboxService::get_singleton()->is_core_script(udata->script->get_path()) &&
			!SandboxService::get_singleton()->resource_has_access(path, SandboxService::RESOURCE_READ_ONLY)) {
		luaL_error(L, "Cannot load Resource at %s: no permissions", path.utf8().get_data());
	}

	Ref<Resource> res = nb::ResourceLoader::get_singleton_nb()->load(path);
	LuaStackOp<Object *>::push(L, res.ptr());
	return 1;
}

static int luaGD_save(lua_State *L) {
	Ref<Resource> res = Ref<Resource>(LuaStackOp<Object *>::check(L, 1));
	String path = LuaStackOp<String>::check(L, 2);
	int flags = luaL_optinteger(L, 3, 0);

	GDThreadData *udata = luaGD_getthreaddata(L);

	if (SandboxService::get_singleton() &&
			udata->script.is_valid() &&
			!SandboxService::get_singleton()->is_core_script(udata->script->get_path()) &&
			!SandboxService::get_singleton()->resource_has_access(path, SandboxService::RESOURCE_READ_WRITE)) {
		luaL_error(L, "Cannot save Resource at %s: no permissions", path.utf8().get_data());
	}

	Error err = nb::ResourceSaver::get_singleton_nb()->save(res, path, flags);
	lua_pushinteger(L, err);
	return 1;
}

static int luaGD_gdtypeof(lua_State *L) {
	luaL_checkany(L, 1);

	int type = LuaStackOp<Variant>::get_type(L, 1);
	if (type == -1) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, type);
	}

	return 1;
}

static int luaGD_tointeger(lua_State *L) {
	double num = luaL_checknumber(L, 1);
	lua_pushinteger(L, static_cast<int>(num));
	return 1;
}

static int luaGD_tofloat(lua_State *L) {
	double num = luaL_checknumber(L, 1);

	double int_part = 0.0;
	if (std::modf(num, &int_part) != 0.0) {
		return 1;
	}

	lua_pushnumber(L, std::nextafter(num, DBL_MAX));
	return 1;
}

void luaGD_gderror(const char *p_method, const String &p_path, String p_msg, int p_line) {
	String file;
	int line = 0;

	if (p_line > 0) {
		file = p_path.is_empty() ? "built-in" : p_path;
		line = p_line;
	} else {
		PackedStringArray split = p_msg.split(":");

		if (p_msg.begins_with("res://")) {
			// Expect : after res, then 2 more for regular Lua error
			file = String(":").join(split.slice(0, 2));
			line = split[2].to_int();
			p_msg = String(":").join(split.slice(3)).substr(1);
		} else {
			// Chunk name does not include :
			file = split[0];
			line = split[1].to_int();
			p_msg = String(":").join(split.slice(2)).substr(1);
		}
	}

	// TODO: Switch back to script error when debugger is implemented
	/*
	internal::gdextension_interface_print_script_error(
			p_msg.utf8().get_data(),
			p_method,
			file.utf8().get_data(),
			line,
			false);
	*/
	_err_print_error(p_method, file.utf8().get_data(), line, p_msg);
}

const Luau::CompileOptions &luaGD_compileopts() {
	static LocalVector<const char *> mutable_globals;
	static Luau::CompileOptions opts;
	static bool did_init = false;

	if (!did_init) {
		for (const ApiClass &g_class : get_extension_api().classes) {
			if (!g_class.singleton || g_class.properties.size() == 0)
				continue;

			for (const KeyValue<String, ApiClassProperty> &E : g_class.properties) {
				if (!E.value.setter.is_empty()) {
					mutable_globals.push_back(g_class.name);
					break;
				}
			}
		}

		mutable_globals.push_back(nullptr);

		// Prevents Luau from optimizing the value such that it (seemingly) won't ever change
		opts.mutableGlobals = mutable_globals.ptr();
	}

	return opts;
}

static const luaL_Reg global_funcs[] = {
	{ "SN", luaGD_str_ctor<StringName> },
	{ "NP", luaGD_str_ctor<NodePath> },
	{ "I64", luaGD_i64_ctor },

	{ "load", luaGD_load },
	{ "save", luaGD_save },

	{ "gdtypeof", luaGD_gdtypeof },

	{ "tointeger", luaGD_tointeger },
	{ "tofloat", luaGD_tofloat },

	{ nullptr, nullptr }
};

void luaGD_openlibs(lua_State *L) {
	LuaStackOp<int64_t>::init_metatable(L);
	luaL_register(L, "_G", global_funcs);
}
