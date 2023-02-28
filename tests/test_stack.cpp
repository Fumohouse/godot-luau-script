#include <catch_amalgamated.hpp>

#include <lua.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "luagd_bindings_stack.gen.h"
#include "luagd_stack.h"
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

#define CHECK_ARR(type)                                          \
    {                                                            \
        REQUIRE(LuaStackOp<type>::is(L, -1));                    \
        REQUIRE(LuaStackOp<type>::get(L, -1) == expected_arr);   \
        REQUIRE(LuaStackOp<type>::check(L, -1) == expected_arr); \
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
                    CHECK_ARR(PackedStringArray));
        }

        SECTION("coerced") {
            EVAL_THEN(L, R"ASDF(
                return { "1", "2", "3" }
            )ASDF",
                    CHECK_ARR(PackedStringArray));
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
                        CHECK_ARR(Array));
            }

            SECTION("coerced") {
                EVAL_THEN(L, R"ASDF(
                    return { "1", 2.5, Vector2.new(3, 4) }
                )ASDF",
                        CHECK_ARR(Array));
            }
        }
    }
}
