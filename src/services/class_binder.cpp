#include "services/class_binder.h"

#include <lua.h>
#include <lualib.h>

#include "core/godot_bindings.h"
#include "core/lua_utils.h"

using namespace godot;

int LuaGDClass::lua_namecall(lua_State *L) {
	const LuaGDClass *l_class = luaGD_lightudataup<LuaGDClass>(L, 1);

	if (const char *name = lua_namecallatom(L, nullptr)) {
		HashMap<String, Method>::ConstIterator E = l_class->methods.find(name);
		if (E) {
			return E->value.func(L);
		}

		luaGD_nomethoderror(L, name, l_class->name);
	}

	luaGD_nonamecallatomerror(L);
}

int LuaGDClass::lua_newindex(lua_State *L) {
	const LuaGDClass *l_class = luaGD_lightudataup<LuaGDClass>(L, 1);
	const char *name = luaL_checkstring(L, 2);
	lua_remove(L, 2); // not used further

	HashMap<String, Property>::ConstIterator E = l_class->properties.find(name);
	if (E) {
		if (!E->value.setter) {
			luaGD_propreadonlyerror(L, name);
		}

		return E->value.setter(L);
	}

	luaGD_indexerror(L, name, l_class->name);
}

int LuaGDClass::lua_index(lua_State *L) {
	const LuaGDClass *l_class = luaGD_lightudataup<LuaGDClass>(L, 1);
	const char *name = luaL_checkstring(L, 2);
	lua_remove(L, 2); // not used further

	if (l_class->index_override) {
		if (int nret = l_class->index_override(L, name)) {
			return nret;
		}
	}

	HashMap<String, Property>::ConstIterator E = l_class->properties.find(name);
	if (E) {
		if (!E->value.getter) {
			luaGD_propwriteonlyerror(L, name);
		}

		return E->value.getter(L);
	}

	HashMap<String, Method>::ConstIterator F = l_class->static_methods.find(name);
	if (!F) {
		F = l_class->methods.find(name);
	}

	if (F) {
		lua_pushcfunction(L, F->value.func, F->value.debug_name.utf8().get_data());
		return 1;
	}

	luaGD_indexerror(L, name, l_class->name);
}

String LuaGDClass::get_debug_name(const String &p_name) {
	return String(metatable_name) + '.' + p_name;
}

void LuaGDClass::set_name(const char *p_name, const char *p_metatable_name) {
	name = p_name;
	metatable_name = p_metatable_name;
}

void LuaGDClass::bind_property(const char *p_name, lua_CFunction setter, lua_CFunction getter) {
	properties.insert(p_name, { setter, getter });
}

void LuaGDClass::set_index_override(IndexOverride p_func) {
	index_override = p_func;
}

void LuaGDClass::init_metatable(lua_State *L) const {
	if (!luaL_newmetatable(L, metatable_name)) {
		luaL_error(L, "metatable '%s' already exists", metatable_name);
	}

	lua_pushlightuserdata(L, (void *)this);
	lua_pushvalue(L, -1);
	lua_pushcclosure(L, lua_namecall, "LuaGDClass.__namecall", 1);
	lua_setfield(L, -3, "__namecall");

	lua_pushvalue(L, -1);
	lua_pushcclosure(L, lua_newindex, "LuaGDClass.__newindex", 1);
	lua_setfield(L, -3, "__newindex");

	lua_pushcclosure(L, lua_index, "LuaGDClass.__index", 1);
	lua_setfield(L, -2, "__index");

	lua_pop(L, 1); // metatable
}
