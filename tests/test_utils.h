#pragma once

#include <catch_amalgamated.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

// Used in macros
#include "core/stack.h" // IWYU pragma: keep

using namespace godot;

struct lua_State;

class LuauFixture {
protected:
	lua_State *L;

public:
	LuauFixture();
	~LuauFixture();
};

struct ExecOutput {
	Error status;
	String error;
};

ExecOutput luaGD_exec(lua_State *L, const char *src);

#define EVAL_THEN(L, m_src, m_expr)            \
	{                                          \
		int top = lua_gettop(L);               \
		ExecOutput out = luaGD_exec(L, m_src); \
                                               \
		if (out.status != OK)                  \
			FAIL(out.error.utf8().get_data()); \
                                               \
		m_expr;                                \
                                               \
		lua_settop(L, top);                    \
	}

#define ASSERT_EVAL_EQ(L, m_src, m_type, m_value)           \
	EVAL_THEN(L, m_src, {                                   \
		CHECK(LuaStackOp<m_type>::check(L, -1) == m_value); \
	})

#define ASSERT_EVAL_OK(L, m_src) EVAL_THEN(L, m_src, {})

#define ASSERT_EVAL_FAIL(L, m_src, m_err)      \
	{                                          \
		ExecOutput out = luaGD_exec(L, m_src); \
                                               \
		REQUIRE(out.status != OK);             \
		INFO(out.error.utf8().get_data());     \
		REQUIRE(out.error == m_err);           \
	}

#define LOAD_SCRIPT_FILE_BASE(m_name, m_path, m_expr)                        \
	Ref<LuauScript> m_name;                                                  \
	{                                                                        \
		Error error;                                                         \
		m_name = luau_cache.get_script("res://test_scripts/" m_path, error); \
		REQUIRE(error == OK);                                                \
		m_expr;                                                              \
	}

#define LOAD_SCRIPT_FILE(m_name, m_path) LOAD_SCRIPT_FILE_BASE(m_name, m_path, { REQUIRE(m_name->_is_valid()); })
#define LOAD_SCRIPT_MOD_FILE(m_name, m_path) LOAD_SCRIPT_FILE_BASE(m_name, m_path, {})
