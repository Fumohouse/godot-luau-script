#include "core/base_lib.h"

#include <lua.h>
#include <lualib.h>
#include <cfloat>
#include <cmath>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/lua_utils.h"
#include "core/stack.h"
#include "services/sandbox_service.h"
#include "utils/wrapped_no_binding.h"

using namespace godot;

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
