#include "core/lua_utils.h"

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/local_vector.hpp>

#include "core/base_lib.h"
#include "core/extension_api.h"
#include "core/godot_bindings.h"
#include "core/permissions.h"
#include "core/runtime.h"

using namespace godot;

// Based on the default implementation seen in the Lua 5.1 reference
static void *luaGD_alloc(void *, void *p_ptr, size_t, size_t p_nsize) {
	if (p_nsize == 0) {
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
		udata->lock = parent_udata->lock;
		udata->script = parent_udata->script;
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

lua_State *luaGD_newstate(LuauRuntime::VMType p_vm_type, BitField<ThreadPermissions> p_base_permissions) {
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
