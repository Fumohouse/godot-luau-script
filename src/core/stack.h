#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

#define luaGD_mtnotfounderror(L, p_name) luaL_error(L, "metatable not found: '%s'", p_name)

template <typename T>
struct LuaStackOp {};

#define STACK_OP_DEF(m_type)                                   \
	template <>                                                \
	struct LuaStackOp<m_type> {                                \
		static void push(lua_State *L, const m_type &p_value); \
                                                               \
		static m_type get(lua_State *L, int p_index);          \
		static bool is(lua_State *L, int p_index);             \
		static m_type check(lua_State *L, int p_index);        \
	};

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

#define UDATA_STACK_OP_IMPL(m_type, m_metatable_name, m_dtor)          \
	UDATA_ALLOC(m_type, m_metatable_name, m_dtor)                      \
                                                                       \
	void LuaStackOp<m_type>::push(lua_State *L, const m_type &value) { \
		m_type *udata = LuaStackOp<m_type>::alloc(L);                  \
		*udata = value;                                                \
	}                                                                  \
                                                                       \
	bool LuaStackOp<m_type>::is(lua_State *L, int p_index) {           \
		return luaGD_metatables_match(L, p_index, m_metatable_name);   \
	}                                                                  \
                                                                       \
	UDATA_GET_PTR(m_type, m_metatable_name)                            \
                                                                       \
	m_type LuaStackOp<m_type>::get(lua_State *L, int p_index) {        \
		m_type *udata = LuaStackOp<m_type>::get_ptr(L, p_index);       \
		if (!udata)                                                    \
			return m_type();                                           \
                                                                       \
		return *udata;                                                 \
	}                                                                  \
                                                                       \
	UDATA_CHECK_PTR(m_type, m_metatable_name)                          \
                                                                       \
	m_type LuaStackOp<m_type>::check(lua_State *L, int p_index) {      \
		return *LuaStackOp<m_type>::check_ptr(L, p_index);             \
	}

#define NO_DTOR [](void *) {}
#define DTOR(m_type)                                    \
	[](void *p_udata) {                                 \
		reinterpret_cast<m_type *>(p_udata)->~m_type(); \
	}

#include "core/builtins_stack.gen.inc"
