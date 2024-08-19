#include <catch_amalgamated.hpp>

#include <lua.h>
#include <godot_cpp/variant/builtin_types.hpp>

#include "core/stack.h"
#include "test_utils.h"

using namespace godot;

template <typename T, typename U>
static inline void test_stack_op(lua_State *L, U p_value) {
	int top = lua_gettop(L);

	LuaStackOp<T>::push(L, p_value);
	REQUIRE(lua_gettop(L) == top + 1);

	REQUIRE(LuaStackOp<T>::is(L, -1));
	REQUIRE(lua_gettop(L) == top + 1);

	REQUIRE(LuaStackOp<T>::get(L, -1) == p_value);
	REQUIRE(lua_gettop(L) == top + 1);

	REQUIRE(LuaStackOp<T>::check(L, -1) == p_value);
	REQUIRE(lua_gettop(L) == top + 1);

	lua_pop(L, 1);
}

TEST_CASE_METHOD(LuauFixture, "vm: stack operations") {
	test_stack_op<bool>(L, true);
	test_stack_op<int>(L, 12);
	test_stack_op<String>(L, "hello there! おはようございます");
	test_stack_op<Transform3D>(L, Transform3D().rotated(Vector3(1, 1, 1).normalized(), 2));

	PackedStringArray arr = { "1", "2", "3" };
	test_stack_op<PackedStringArray>(L, arr);

	Array var_arr;
	var_arr.push_back("1");
	var_arr.push_back(2.5);
	var_arr.push_back(Vector2(3, 4));
	test_stack_op<Array>(L, var_arr);

	Dictionary dict;
	dict["one"] = 1;
	dict["two"] = 2;
	dict["three"] = 3;
	test_stack_op<Dictionary>(L, dict);
}

TEST_CASE_METHOD(LuauFixture, "vm: string coercion") {
	SECTION("StringName"){
		ASSERT_EVAL_EQ(L, "return 'hey'", StringName, StringName("hey"))
	}

	SECTION("NodePath") {
		ASSERT_EVAL_EQ(L, "return '../Node'", NodePath, NodePath("../Node"))
	}
}

TEST_CASE_METHOD(LuauFixture, "vm: 64-bit integers") {
	SECTION("|x| <= 2^53") {
		int64_t i = 9007199254740992;
		test_stack_op<int64_t>(L, i);

		LuaStackOp<int64_t>::push(L, i);
		REQUIRE(LuaStackOp<Variant>::get_type(L, -1) == GDEXTENSION_VARIANT_TYPE_INT);
		REQUIRE(lua_isnumber(L, -1));
	}

	SECTION("|x| > 2^53") {
		int64_t j = 9007199254740993;
		test_stack_op<int64_t>(L, j);

		LuaStackOp<int64_t>::push(L, j);
		REQUIRE(LuaStackOp<Variant>::get_type(L, -1) == GDEXTENSION_VARIANT_TYPE_INT);
		REQUIRE(lua_isuserdata(L, -1));
	}
}
