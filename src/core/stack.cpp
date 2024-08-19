#include "core/stack.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <cmath>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/godot_bindings.h"
#include "core/variant.h"
#include "utils/utils.h"
#include "utils/wrapped_no_binding.h"

using namespace godot;

bool luaGD_metatables_match(lua_State *L, int p_index, const char *p_metatable_name) {
	if (lua_type(L, p_index) != LUA_TUSERDATA || !lua_getmetatable(L, p_index))
		return false;

	luaL_getmetatable(L, p_metatable_name);
	if (lua_isnil(L, -1))
		luaGD_mtnotfounderror(L, p_metatable_name);

	bool result = lua_equal(L, -1, -2);
	lua_pop(L, 2);

	return result;
}

/* BASIC TYPES */

#define BASIC_STACK_OP_IMPL(m_type, m_op_name, m_is_name)                                                                    \
	void LuaStackOp<m_type>::push(lua_State *L, const m_type &p_value) { lua_push##m_op_name(L, p_value); }                  \
	m_type LuaStackOp<m_type>::get(lua_State *L, int p_index) { return static_cast<m_type>(lua_to##m_op_name(L, p_index)); } \
	bool LuaStackOp<m_type>::is(lua_State *L, int p_index) { return lua_is##m_is_name(L, p_index); }                         \
	m_type LuaStackOp<m_type>::check(lua_State *L, int p_index) { return static_cast<m_type>(luaL_check##m_op_name(L, p_index)); }

BASIC_STACK_OP_IMPL(bool, boolean, boolean);
BASIC_STACK_OP_IMPL(int, integer, number);
BASIC_STACK_OP_IMPL(float, number, number);
BASIC_STACK_OP_IMPL(double, number, number);
BASIC_STACK_OP_IMPL(int8_t, number, number);
BASIC_STACK_OP_IMPL(uint8_t, unsigned, number);
BASIC_STACK_OP_IMPL(int16_t, number, number);
BASIC_STACK_OP_IMPL(uint16_t, unsigned, number);
BASIC_STACK_OP_IMPL(uint32_t, unsigned, number);

#define INT64_MT_NAME "Luau.Int64"
#define INT64_DOUBLE_LIMIT 9007199254740992 // 2^53; see https://stackoverflow.com/a/1848762

void LuaStackOp<int64_t>::init_metatable(lua_State *L) {
	luaL_newmetatable(L, INT64_MT_NAME);

	lua_pushstring(L, "Int64");
	lua_setfield(L, -2, "__type");

#define INT64_OP_FUNC(m_exp, m_name, m_ret)                                                             \
	lua_pushcfunction(                                                                                  \
			L, [](lua_State *L) {                                                                       \
				double d1 = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : LuaStackOp<int64_t>::check(L, 1); \
				double d2 = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : LuaStackOp<int64_t>::check(L, 2); \
				LuaStackOp<m_ret>::push(L, m_exp);                                                      \
				return 1;                                                                               \
			},                                                                                          \
			INT64_MT_NAME "." m_name);                                                                  \
                                                                                                        \
	lua_setfield(L, -2, m_name);

	INT64_OP_FUNC(d1 + d2, "__add", double)
	INT64_OP_FUNC(d1 - d2, "__sub", double)
	INT64_OP_FUNC(d1 * d2, "__mul", double)
	INT64_OP_FUNC(d1 / d2, "__div", double)
	INT64_OP_FUNC(fmod(d1, d2), "__mod", double)
	INT64_OP_FUNC(pow(d1, d2), "__pow", double)

	INT64_OP_FUNC(d1 == d2, "__eq", bool)
	INT64_OP_FUNC(d1 < d2, "__lt", bool)

	// Special: negate
	lua_pushcfunction(
			L, [](lua_State *L) {
				LuaStackOp<int64_t>::push(L, -LuaStackOp<int64_t>::check(L, 1));
				return 1;
			},
			INT64_MT_NAME ".__unm");

	lua_setfield(L, -2, "__unm");

	// Special: stringify
	lua_pushcfunction(
			L, [](lua_State *L) {
				LuaStackOp<String>::push(L, String::num_int64(LuaStackOp<int64_t>::check(L, 1)));
				return 1;
			},
			INT64_MT_NAME ".__tostring");

	lua_setfield(L, -2, "__tostring");

	lua_setreadonly(L, -1, true);
}

void LuaStackOp<int64_t>::push_i64(lua_State *L, const int64_t &p_value) {
	int64_t *udata = reinterpret_cast<int64_t *>(lua_newuserdata(L, sizeof(int64_t)));
	*udata = p_value;

	luaL_getmetatable(L, INT64_MT_NAME);
	if (lua_isnil(L, -1))
		luaGD_mtnotfounderror(L, INT64_MT_NAME);

	lua_setmetatable(L, -2);
}

void LuaStackOp<int64_t>::push(lua_State *L, const int64_t &p_value) {
	if (p_value >= -INT64_DOUBLE_LIMIT && p_value <= INT64_DOUBLE_LIMIT) {
		lua_pushnumber(L, p_value);
	} else {
		LuaStackOp<int64_t>::push_i64(L, p_value);
	}
}

int64_t LuaStackOp<int64_t>::get(lua_State *L, int p_index) {
	if (lua_isnumber(L, p_index))
		return lua_tonumber(L, p_index);

	if (luaGD_metatables_match(L, p_index, INT64_MT_NAME))
		return *reinterpret_cast<int64_t *>(lua_touserdata(L, p_index));

	return 0;
}

bool LuaStackOp<int64_t>::is(lua_State *L, int p_index) {
	return lua_isnumber(L, p_index) || luaGD_metatables_match(L, p_index, INT64_MT_NAME);
}

int64_t LuaStackOp<int64_t>::check(lua_State *L, int p_index) {
	if (luaGD_metatables_match(L, p_index, INT64_MT_NAME))
		return *reinterpret_cast<int64_t *>(lua_touserdata(L, p_index));

	return luaL_checknumber(L, p_index);
}

/* STRING */

void LuaStackOp<String>::push(lua_State *L, const String &p_value) {
	lua_pushstring(L, p_value.utf8().get_data());
}

String LuaStackOp<String>::get(lua_State *L, int p_index) {
	return String::utf8(lua_tostring(L, p_index));
}

bool LuaStackOp<String>::is(lua_State *L, int p_index) {
	return lua_isstring(L, p_index);
}

String LuaStackOp<String>::check(lua_State *L, int p_index) {
	return String::utf8(luaL_checkstring(L, p_index));
}

/* OBJECTS */

struct ObjectUdata {
	uint64_t id = 0;
	bool is_namecall = true;
};

static void luaGD_object_init(GDExtensionObjectPtr p_obj) {
	if (GDExtensionObjectPtr rc = Utils::cast_obj<RefCounted>(p_obj))
		nb::RefCounted(rc).init_ref();
}

static void luaGD_object_dtor(void *p_ptr) {
	ObjectUdata *udata = reinterpret_cast<ObjectUdata *>(p_ptr);
	if (udata->id == 0)
		return;

	GDExtensionObjectPtr rc = Utils::cast_obj<RefCounted>(internal::gdextension_interface_object_get_instance_from_id(udata->id));
	if (rc && nb::RefCounted(rc).unreference())
		internal::gdextension_interface_object_destroy(rc);
}

#define LUAGD_OBJ_CACHE_TABLE "_OBJECTS"

void LuaStackOp<Object *>::push(lua_State *L, GDExtensionObjectPtr p_value) {
	if (!p_value) {
		lua_pushnil(L);
		return;
	}

	lua_getfield(L, LUA_REGISTRYINDEX, LUAGD_OBJ_CACHE_TABLE);

	// Lazy initialize table
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1); // nil

		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, LUAGD_OBJ_CACHE_TABLE);

		// Mark table as weak
		lua_newtable(L);
		lua_pushstring(L, "v");
		lua_setfield(L, -2, "__mode");
		lua_setreadonly(L, -1, true);
		lua_setmetatable(L, -2);
	}

	nb::Object obj = p_value;
	ObjectUdata *udata = nullptr;
	uint64_t id = obj.get_instance_id();
	// Prevent loss of precision (vs casting)
	const char *str_id = reinterpret_cast<const char *>(&id);

	// Check to return from cache
	lua_pushlstring(L, str_id, sizeof(uint64_t));
	lua_gettable(L, -2);

	bool has_cached = !lua_isnil(L, -1);

	if (has_cached) {
		ObjectUdata *cached_udata = reinterpret_cast<ObjectUdata *>(lua_touserdata(L, -1));

		if (cached_udata->is_namecall == !obj.get_script().operator Object *()) {
			// Metatable is correct
			lua_remove(L, -2); // table
			return;
		} else {
			// Metatable should be changed, fall below to update
			udata = cached_udata;
		}
	} else {
		lua_pop(L, 1); // value
	}

	if (!udata)
		udata = reinterpret_cast<ObjectUdata *>(lua_newuserdatadtor(L, sizeof(ObjectUdata), luaGD_object_dtor));

	// Must search parent classes because some classes used in Godot are not registered,
	// e.g. GodotPhysicsDirectSpaceState3D -> PhysicsDirectSpaceState3D
	StringName curr_class = obj.get_class();

	while (!curr_class.is_empty()) {
		String metatable_name = "Godot.Object." + curr_class;
		if (!obj.get_script().operator Object *()) {
			metatable_name = metatable_name + ".Namecall";
			udata->is_namecall = true;
		} else {
			udata->is_namecall = false;
		}

		luaL_getmetatable(L, metatable_name.utf8().get_data());
		if (!lua_isnil(L, -1)) {
			break;
		}

		lua_pop(L, 1); // nil

		curr_class = nb::ClassDB::get_singleton_nb()->get_parent_class(curr_class);
	}

	if (!has_cached)
		luaGD_object_init(p_value);

	udata->id = id;

	// Shouldn't be possible
	if (!lua_istable(L, -1))
		luaL_error(L, "metatable not found for class %s", obj.get_class().utf8().get_data());

	lua_setmetatable(L, -2);

	lua_pushlstring(L, str_id, sizeof(uint64_t));
	lua_pushvalue(L, -2);
	lua_settable(L, -4);
	lua_remove(L, -2); // table
}

void LuaStackOp<Object *>::push(lua_State *L, Object *p_value) {
	LuaStackOp<Object *>::push(L, p_value ? p_value->_owner : nullptr);
}

bool LuaStackOp<Object *>::is(lua_State *L, int p_index) {
	if (lua_isnil(L, p_index))
		return true;

	if (lua_type(L, p_index) != LUA_TUSERDATA || !lua_getmetatable(L, p_index))
		return false;

	lua_getfield(L, -1, MT_CLASS_TYPE);

	bool is_obj = lua_type(L, -1) == LUA_TNUMBER;
	lua_pop(L, 2); // value, metatable

	return is_obj;
}

GDObjectInstanceID *LuaStackOp<Object *>::get_id(lua_State *L, int p_index) {
	ObjectUdata *udata = reinterpret_cast<ObjectUdata *>(lua_touserdata(L, p_index));
	if (!udata)
		return nullptr;

	return &udata->id;
}

GDExtensionObjectPtr LuaStackOp<Object *>::get(lua_State *L, int p_index) {
	if (!LuaStackOp<Object *>::is(L, p_index))
		return nullptr;

	GDObjectInstanceID *udata = LuaStackOp<Object *>::get_id(L, p_index);
	if (!udata || *udata == 0)
		return nullptr;

	return internal::gdextension_interface_object_get_instance_from_id(*udata);
}

GDExtensionObjectPtr LuaStackOp<Object *>::check(lua_State *L, int p_index) {
	if (!LuaStackOp<Object *>::is(L, p_index))
		luaL_typeerrorL(L, p_index, "Object");

	return LuaStackOp<Object *>::get(L, p_index);
}

/* VARIANT */

void LuaStackOp<Variant>::push(lua_State *L, const Variant &p_value) {
	LuauVariant lv;

	lv.initialize(GDExtensionVariantType(p_value.get_type()));
	lv.assign_variant(p_value);
	lv.lua_push(L);
}

Variant LuaStackOp<Variant>::get(lua_State *L, int p_index) {
	int type = LuaStackOp<Variant>::get_type(L, p_index);
	if (type == -1)
		return Variant();

	LuauVariant lv;
	lv.lua_check(L, p_index, GDExtensionVariantType(type));

	return lv.to_variant();
}

bool LuaStackOp<Variant>::is(lua_State *L, int p_index) {
	return LuaStackOp<Variant>::get_type(L, p_index) != -1;
}

int LuaStackOp<Variant>::get_type(lua_State *L, int p_index) {
	switch (lua_type(L, p_index)) {
		case LUA_TNIL:
			return GDEXTENSION_VARIANT_TYPE_NIL;

		case LUA_TBOOLEAN:
			return GDEXTENSION_VARIANT_TYPE_BOOL;

		case LUA_TNUMBER: {
			// Somewhat frail...
			double value = lua_tonumber(L, p_index);
			double int_part = 0.0;

			if (std::modf(value, &int_part) == 0.0)
				return GDEXTENSION_VARIANT_TYPE_INT;
			else
				return GDEXTENSION_VARIANT_TYPE_FLOAT;
		}

		case LUA_TSTRING:
			return GDEXTENSION_VARIANT_TYPE_STRING;

		case LUA_TUSERDATA: {
			// Special case
			if (LuaStackOp<int64_t>::is(L, p_index))
				return GDEXTENSION_VARIANT_TYPE_INT;

			if (!lua_getmetatable(L, p_index))
				return -1;

			lua_getfield(L, -1, MT_VARIANT_TYPE);
			if (lua_isnil(L, -1))
				return -1;

			int type = lua_tonumber(L, -1);
			lua_pop(L, 2); // value, metatable

			return type;
		}

		default:
			return -1;
	}
}

Variant LuaStackOp<Variant>::check(lua_State *L, int p_index) {
	int type = LuaStackOp<Variant>::get_type(L, p_index);
	if (type == -1)
		luaL_typeerrorL(L, p_index, "Variant");

	LuauVariant lv;
	lv.lua_check(L, p_index, GDExtensionVariantType(type));

	return lv.to_variant();
}

/* STRING COERCION */

#define STR_STACK_OP_IMPL(m_type)                                                                       \
	UDATA_ALLOC(m_type, BUILTIN_MT_NAME(m_type), DTOR(m_type))                                          \
                                                                                                        \
	void LuaStackOp<m_type>::push(lua_State *L, const m_type &p_value, bool p_force_type) {             \
		if (p_force_type) {                                                                             \
			m_type *udata = LuaStackOp<m_type>::alloc(L);                                               \
			*udata = p_value;                                                                           \
		} else {                                                                                        \
			LuaStackOp<String>::push(L, String(p_value));                                               \
		}                                                                                               \
	}                                                                                                   \
                                                                                                        \
	bool LuaStackOp<m_type>::is(lua_State *L, int p_index) {                                            \
		return lua_isstring(L, p_index) || luaGD_metatables_match(L, p_index, BUILTIN_MT_NAME(m_type)); \
	}                                                                                                   \
                                                                                                        \
	UDATA_GET_PTR(m_type, BUILTIN_MT_NAME(m_type))                                                      \
                                                                                                        \
	m_type LuaStackOp<m_type>::get(lua_State *L, int p_index) {                                         \
		if (luaGD_metatables_match(L, p_index, BUILTIN_MT_NAME(m_type)))                                \
			return *LuaStackOp<m_type>::get_ptr(L, p_index);                                            \
                                                                                                        \
		return m_type(lua_tostring(L, p_index));                                                        \
	}                                                                                                   \
                                                                                                        \
	UDATA_CHECK_PTR(m_type, BUILTIN_MT_NAME(m_type))                                                    \
                                                                                                        \
	m_type LuaStackOp<m_type>::check(lua_State *L, int p_index) {                                       \
		if (lua_isstring(L, p_index))                                                                   \
			return m_type(lua_tostring(L, p_index));                                                    \
                                                                                                        \
		if (luaGD_metatables_match(L, p_index, BUILTIN_MT_NAME(m_type)))                                \
			return *LuaStackOp<m_type>::get_ptr(L, p_index);                                            \
                                                                                                        \
		luaL_typeerrorL(L, p_index, #m_type " or string");                                              \
	}

STR_STACK_OP_IMPL(StringName)
STR_STACK_OP_IMPL(NodePath)
