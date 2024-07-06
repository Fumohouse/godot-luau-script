#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

#define luaGD_mtnotfounderror(L, p_name) luaL_error(L, "metatable not found: '%s'", p_name)

template <typename T>
struct LuaStackOp {};

#define STACK_OP_DEF_BASE(m_type, m_push_type)               \
	template <>                                              \
	struct LuaStackOp<m_type> {                              \
		static void push(lua_State *L, m_push_type p_value); \
                                                             \
		static m_type get(lua_State *L, int p_index);        \
		static bool is(lua_State *L, int p_index);           \
		static m_type check(lua_State *L, int p_index);      \
	};

#define STACK_OP_DEF(m_type) STACK_OP_DEF_BASE(m_type, const m_type &)

#define STACK_OP_PTR_DEF(m_type)                               \
	template <>                                                \
	struct LuaStackOp<m_type> {                                \
		static void push(lua_State *L, const m_type &p_value); \
                                                               \
		static m_type get(lua_State *L, int p_index);          \
		static bool is(lua_State *L, int p_index);             \
		static m_type check(lua_State *L, int p_index);        \
                                                               \
		/* USERDATA */                                         \
                                                               \
		static m_type *alloc(lua_State *L);                    \
		static m_type *get_ptr(lua_State *L, int p_index);     \
		static m_type *check_ptr(lua_State *L, int p_index);   \
	};

#define STACK_OP_STR_DEF(m_type)                                                        \
	template <>                                                                         \
	struct LuaStackOp<m_type> {                                                         \
		static void push(lua_State *L, const m_type &value, bool p_force_type = false); \
                                                                                        \
		static m_type get(lua_State *L, int p_index);                                   \
		static bool is(lua_State *L, int p_index);                                      \
		static m_type check(lua_State *L, int p_index);                                 \
                                                                                        \
		/* USERDATA */                                                                  \
                                                                                        \
		static m_type *alloc(lua_State *L);                                             \
		static m_type *get_ptr(lua_State *L, int p_index);                              \
		static m_type *check_ptr(lua_State *L, int p_index);                            \
	};

STACK_OP_DEF(bool)
STACK_OP_DEF(int)
STACK_OP_DEF(float)
STACK_OP_DEF(String)

STACK_OP_DEF(double)
STACK_OP_DEF(int8_t)
STACK_OP_DEF(uint8_t)
STACK_OP_DEF(int16_t)
STACK_OP_DEF(uint16_t)
STACK_OP_DEF(uint32_t)

template <>
struct LuaStackOp<int64_t> {
	static void init_metatable(lua_State *L);

	static void push_i64(lua_State *L, const int64_t &p_value);
	static void push(lua_State *L, const int64_t &p_value);

	static int64_t get(lua_State *L, int p_index);
	static bool is(lua_State *L, int p_index);
	static int64_t check(lua_State *L, int p_index);
};

template <>
struct LuaStackOp<Object *> {
	static void push(lua_State *L, GDExtensionObjectPtr p_value);
	static void push(lua_State *L, Object *p_value);

	static GDObjectInstanceID *get_id(lua_State *L, int p_index);
	static GDExtensionObjectPtr get(lua_State *L, int p_index);
	static bool is(lua_State *L, int p_index);
	static GDExtensionObjectPtr check(lua_State *L, int p_index);
};

template <>
struct LuaStackOp<Variant> {
	static void push(lua_State *L, const Variant &p_value);

	static Variant get(lua_State *L, int p_index);
	static bool is(lua_State *L, int p_index);
	static int get_type(lua_State *L, int p_index);
	static Variant check(lua_State *L, int p_index);
};

STACK_OP_STR_DEF(StringName)
STACK_OP_STR_DEF(NodePath)

STACK_OP_PTR_DEF(Array)
STACK_OP_PTR_DEF(Dictionary)

/* USERDATA */

bool luaGD_metatables_match(lua_State *L, int p_index, const char *p_metatable_name);

#define UDATA_ALLOC(m_type, m_metatable_name, m_dtor)                                               \
	m_type *LuaStackOp<m_type>::alloc(lua_State *L) {                                               \
		m_type *udata = reinterpret_cast<m_type *>(lua_newuserdatadtor(L, sizeof(m_type), m_dtor)); \
		new (udata) m_type();                                                                       \
                                                                                                    \
		luaL_getmetatable(L, m_metatable_name);                                                     \
		if (lua_isnil(L, -1))                                                                       \
			luaGD_mtnotfounderror(L, m_metatable_name);                                             \
                                                                                                    \
		lua_setmetatable(L, -2);                                                                    \
                                                                                                    \
		return udata;                                                                               \
	}

#define UDATA_PUSH(m_type)                                             \
	void LuaStackOp<m_type>::push(lua_State *L, const m_type &value) { \
		m_type *udata = LuaStackOp<m_type>::alloc(L);                  \
		*udata = value;                                                \
	}

#define UDATA_GET_PTR(m_type, m_metatable_name)                        \
	m_type *LuaStackOp<m_type>::get_ptr(lua_State *L, int p_index) {   \
		if (!luaGD_metatables_match(L, p_index, m_metatable_name))     \
			return nullptr;                                            \
                                                                       \
		return reinterpret_cast<m_type *>(lua_touserdata(L, p_index)); \
	}

#define UDATA_CHECK_PTR(m_type, m_metatable_name)                                         \
	m_type *LuaStackOp<m_type>::check_ptr(lua_State *L, int p_index) {                    \
		return reinterpret_cast<m_type *>(luaL_checkudata(L, p_index, m_metatable_name)); \
	}

#define UDATA_STACK_OP_IMPL(m_type, m_metatable_name, m_dtor)        \
	UDATA_ALLOC(m_type, m_metatable_name, m_dtor)                    \
	UDATA_PUSH(m_type)                                               \
                                                                     \
	bool LuaStackOp<m_type>::is(lua_State *L, int p_index) {         \
		return luaGD_metatables_match(L, p_index, m_metatable_name); \
	}                                                                \
                                                                     \
	UDATA_GET_PTR(m_type, m_metatable_name)                          \
                                                                     \
	m_type LuaStackOp<m_type>::get(lua_State *L, int p_index) {      \
		m_type *udata = LuaStackOp<m_type>::get_ptr(L, p_index);     \
		if (!udata)                                                  \
			return m_type();                                         \
                                                                     \
		return *udata;                                               \
	}                                                                \
                                                                     \
	UDATA_CHECK_PTR(m_type, m_metatable_name)                        \
                                                                     \
	m_type LuaStackOp<m_type>::check(lua_State *L, int p_index) {    \
		return *LuaStackOp<m_type>::check_ptr(L, p_index);           \
	}

#define NO_DTOR [](void *) {}
#define DTOR(m_type)                                    \
	[](void *p_udata) {                                 \
		reinterpret_cast<m_type *>(p_udata)->~m_type(); \
	}

/* ARRAY */

bool luaGD_isarray(lua_State *L, int p_index, const char *p_metatable_name, Variant::Type p_type, const String &p_class_name);

template <typename TArray>
struct ArraySetter {
	typedef void (*ArraySet)(TArray &p_array, int p_index, Variant p_elem);
};

template <typename TArray>
TArray luaGD_getarray(lua_State *L, int p_index, const char *p_metatable_name, Variant::Type p_type, const String &p_class_name, typename ArraySetter<TArray>::ArraySet p_setter) {
	if (luaGD_metatables_match(L, p_index, p_metatable_name))
		return *LuaStackOp<TArray>::get_ptr(L, p_index);

	if (!lua_istable(L, p_index))
		return TArray();

	p_index = lua_absindex(L, p_index);

	int len = lua_objlen(L, p_index);

	TArray arr;
	arr.resize(len);

	for (int i = 0; i < len; i++) {
		lua_pushinteger(L, i + 1);
		lua_gettable(L, p_index);

		Variant elem = LuaStackOp<Variant>::get(L, -1);
		lua_pop(L, 1);

		if (p_type != Variant::NIL &&
				(elem.get_type() != p_type ||
						(p_type == Variant::OBJECT && !elem.operator Object *()->is_class(p_class_name)))) {
			return TArray();
		}

		p_setter(arr, i, elem);
	}

	return arr;
}

template <typename TArray>
TArray luaGD_checkarray(lua_State *L, int p_index, const char *p_arr_type_name, const char *p_metatable_name, Variant::Type p_type, const String &p_class_name, typename ArraySetter<TArray>::ArraySet p_setter) {
	if (luaGD_metatables_match(L, p_index, p_metatable_name))
		return *LuaStackOp<TArray>::get_ptr(L, p_index);

	if (!lua_istable(L, p_index))
		luaL_typeerrorL(L, p_index, p_arr_type_name);

	p_index = lua_absindex(L, p_index);

	int len = lua_objlen(L, p_index);

	TArray arr;
	arr.resize(len);

	for (int i = 0; i < len; i++) {
		lua_pushinteger(L, i + 1);
		lua_gettable(L, p_index);

		Variant elem = LuaStackOp<Variant>::get(L, -1);
		lua_pop(L, 1);

		Variant::Type elem_type = elem.get_type();

		Object *obj = elem;

		if (p_type != Variant::NIL &&
				(elem_type != p_type ||
						(p_type == Variant::OBJECT && !obj->is_class(p_class_name)))) {
			String elem_type_name, expected_type_name;

			if (p_type == Variant::OBJECT) {
				elem_type_name = obj->get_class();
				expected_type_name = p_class_name;
			} else {
				elem_type_name = Variant::get_type_name(elem_type);
				expected_type_name = Variant::get_type_name(p_type);
			}

			luaL_error(L, "expected type %s for typed array element, got %s (index %d)",
					expected_type_name.utf8().get_data(),
					elem_type_name.utf8().get_data(),
					i);
		}

		p_setter(arr, i, elem);
	}

	return arr;
}

#define ARRAY_STACK_OP_IMPL(m_type, m_variant_type, m_elem_type, m_metatable_name)                                \
	static void m_type##_set(m_type &p_array, int p_index, Variant p_elem) {                                      \
		p_array[p_index] = p_elem.operator m_elem_type();                                                         \
	}                                                                                                             \
                                                                                                                  \
	UDATA_ALLOC(m_type, m_metatable_name, DTOR(m_type))                                                           \
	UDATA_PUSH(m_type)                                                                                            \
                                                                                                                  \
	bool LuaStackOp<m_type>::is(lua_State *L, int p_index) {                                                      \
		return luaGD_isarray(L, p_index, m_metatable_name, m_variant_type, "");                                   \
	}                                                                                                             \
                                                                                                                  \
	UDATA_GET_PTR(m_type, m_metatable_name)                                                                       \
                                                                                                                  \
	m_type LuaStackOp<m_type>::get(lua_State *L, int p_index) {                                                   \
		return luaGD_getarray<m_type>(L, p_index, m_metatable_name, m_variant_type, "", m_type##_set);            \
	}                                                                                                             \
                                                                                                                  \
	UDATA_CHECK_PTR(m_type, m_metatable_name)                                                                     \
                                                                                                                  \
	m_type LuaStackOp<m_type>::check(lua_State *L, int p_index) {                                                 \
		return luaGD_checkarray<m_type>(L, p_index, #m_type, m_metatable_name, m_variant_type, "", m_type##_set); \
	}

#include "core/builtins_stack.gen.inc"
