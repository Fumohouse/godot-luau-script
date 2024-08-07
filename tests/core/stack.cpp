#include <catch_amalgamated.hpp>

#include <lua.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "core/stack.h"
#include "godot_cpp/variant/dictionary.hpp"
#include "test_utils.h"

using namespace godot;

#define TEST_STACK_OP(type, value)                      \
	{                                                   \
		LuaStackOp<type>::push(L, value);               \
		REQUIRE(LuaStackOp<type>::get(L, -1) == value); \
		lua_pop(L, 1);                                  \
	}

TEST_CASE_METHOD(LuauFixture, "vm: stack operations") {
	// Semicolons please the formatter
	SECTION("simple operations") {
		TEST_STACK_OP(bool, true);
		TEST_STACK_OP(int, 12);
		TEST_STACK_OP(String, "hello there! おはようございます");
		TEST_STACK_OP(Transform3D, Transform3D().rotated(Vector3(1, 1, 1).normalized(), 2));
	}

	SECTION("string coercion") {
		SECTION("StringName"){
			ASSERT_EVAL_EQ(L, "return 'hey'", StringName, StringName("hey"))
		}

		SECTION("NodePath") {
			ASSERT_EVAL_EQ(L, "return '../Node'", NodePath, NodePath("../Node"))
		}
	}
}

#define CHECK_OP(m_type, m_expected)                             \
	{                                                            \
		REQUIRE(LuaStackOp<m_type>::is(L, -1));                  \
		REQUIRE(LuaStackOp<m_type>::get(L, -1) == m_expected);   \
		REQUIRE(LuaStackOp<m_type>::check(L, -1) == m_expected); \
	}

TEST_CASE_METHOD(LuauFixture, "vm: array stack operations") {
	SECTION("packed array") {
		PackedStringArray expected_arr;
		expected_arr.push_back("1");
		expected_arr.push_back("2");
		expected_arr.push_back("3");

		SECTION("Godot") {
			EVAL_THEN(L, R"ASDF(
                local arr = PackedStringArray.new()
                arr:PushBack("1")
                arr:PushBack("2")
                arr:PushBack("3")

                return arr
            )ASDF",
					CHECK_OP(PackedStringArray, expected_arr));
		}

		SECTION("coerced") {
			EVAL_THEN(L, R"ASDF(
                return { "1", "2", "3" }
            )ASDF",
					CHECK_OP(PackedStringArray, expected_arr));
		}
	}

	SECTION("variant array") {
		SECTION("untyped") {
			Array expected_arr;
			expected_arr.push_back("1");
			expected_arr.push_back(2.5);
			expected_arr.push_back(Vector2(3, 4));

			SECTION("Godot") {
				EVAL_THEN(L, R"ASDF(
                    local arr = Array.new()
                    arr:PushBack("1")
                    arr:PushBack(2.5)
                    arr:PushBack(Vector2.new(3, 4))

                    return arr
                )ASDF",
						CHECK_OP(Array, expected_arr));
			}

			SECTION("coerced") {
				EVAL_THEN(L, R"ASDF(
                    return { "1", 2.5, Vector2.new(3, 4) }
                )ASDF",
						CHECK_OP(Array, expected_arr));
			}
		}
	}
}

TEST_CASE_METHOD(LuauFixture, "vm: dictionary stack operations") {
	Dictionary expected_dict;
	expected_dict["one"] = 1;
	expected_dict["two"] = 2;
	expected_dict["three"] = 3;

	SECTION("Godot") {
		EVAL_THEN(L, R"ASDF(
            local dict = Dictionary.new()
            dict:Set("one", 1)
            dict:Set("two", 2)
            dict:Set("three", 3)

            return dict
        )ASDF",
				CHECK_OP(Dictionary, expected_dict));
	}

	SECTION("coerced") {
		EVAL_THEN(L, R"ASDF(
            return { one = 1, two = 2, three = 3 }
        )ASDF",
				CHECK_OP(Dictionary, expected_dict));
	}
}

TEST_CASE_METHOD(LuauFixture, "vm: 64-bit integers") {
	SECTION("|x| <= 2^53") {
		int64_t i = 9007199254740992;
		LuaStackOp<int64_t>::push(L, i);
		REQUIRE(LuaStackOp<int64_t>::is(L, -1));
		REQUIRE(LuaStackOp<Variant>::get_type(L, -1) == GDEXTENSION_VARIANT_TYPE_INT);
		REQUIRE(lua_isnumber(L, -1));
		REQUIRE(LuaStackOp<int64_t>::get(L, -1) == i);
	}

	SECTION("|x| > 2^53") {
		int64_t j = 9007199254740993;
		LuaStackOp<int64_t>::push(L, j);
		REQUIRE(LuaStackOp<int64_t>::is(L, -1));
		REQUIRE(LuaStackOp<Variant>::get_type(L, -1) == GDEXTENSION_VARIANT_TYPE_INT);
		REQUIRE(lua_isuserdata(L, -1));
		REQUIRE(LuaStackOp<int64_t>::get(L, -1) == j);
	}
}
