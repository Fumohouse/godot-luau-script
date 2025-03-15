#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/stack.h"
#include "services/class_binder.h"

#define STACK_OP_SVC_DEF(m_type)                         \
	template <>                                          \
	struct LuaStackOp<m_type *> {                        \
		static void push(lua_State *L, m_type *p_value); \
                                                         \
		static m_type *get(lua_State *L, int p_index);   \
		static bool is(lua_State *L, int p_index);       \
		static m_type *check(lua_State *L, int p_index); \
	};

#define SVC_STACK_OP_IMPL(m_type, m_metatable_name, m_tag)                                                          \
	void LuaStackOp<m_type *>::push(lua_State *L, m_type *p_value) {                                                \
		m_type **udata = reinterpret_cast<m_type **>(lua_newuserdatataggedwithmetatable(L, sizeof(void *), m_tag)); \
		*udata = p_value;                                                                                           \
	}                                                                                                               \
                                                                                                                    \
	m_type *LuaStackOp<m_type *>::get(lua_State *L, int p_index) {                                                  \
		m_type **udata = reinterpret_cast<m_type **>(lua_touserdatatagged(L, p_index, m_tag));                      \
		return udata ? *udata : nullptr;                                                                            \
	}                                                                                                               \
                                                                                                                    \
	bool LuaStackOp<m_type *>::is(lua_State *L, int p_index) {                                                      \
		return lua_touserdatatagged(L, p_index, m_tag);                                                             \
	}                                                                                                               \
                                                                                                                    \
	m_type *LuaStackOp<m_type *>::check(lua_State *L, int p_index) {                                                \
		m_type **udata = reinterpret_cast<m_type **>(lua_touserdatatagged(L, p_index, m_tag));                      \
		if (udata)                                                                                                  \
			return *udata;                                                                                          \
		luaL_typeerrorL(L, p_index, m_metatable_name);                                                              \
	}

class Service {
protected:
	virtual const LuaGDClass &get_lua_class() const = 0;

public:
	const char *get_name() const { return get_lua_class().get_name(); }

	virtual void lua_push(lua_State *L) = 0;
	virtual void register_metatables(lua_State *L);

	virtual ~Service() {}
};

class LuauInterface : public Service {
	static LuauInterface *singleton;

	HashMap<String, Service *> services;

	const LuaGDClass &get_lua_class() const override;

	void register_service(Service *p_svc);
	void init_services();
	void deinit_services();
	static int index_override(lua_State *L, const char *p_name);

public:
	static LuauInterface *get_singleton() { return singleton; }

	void lua_push(lua_State *L) override;
	void register_metatables(lua_State *L) override;

	LuauInterface();
	~LuauInterface();
};

STACK_OP_SVC_DEF(LuauInterface)
