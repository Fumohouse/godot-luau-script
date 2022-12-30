#include <catch_amalgamated.hpp>

#include <lua.h>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "builtins: constructor") {
    ASSERT_EVAL_EQ(L, "return Vector3(1, 2, 3)", Vector3, Vector3(1, 2, 3));
    ASSERT_EVAL_EQ(L, "return Vector3(Vector3i(4, 5, 6))", Vector3, Vector3(4, 5, 6));
}

TEST_CASE_METHOD(LuauFixture, "builtins: methods/functions") {
    SECTION("namecall style") {
        ASSERT_EVAL_EQ(L, "return Vector2(1, 2):Dot(Vector2(1, 2))", float, 5);
    }

    SECTION("invoked from global table") {
        ASSERT_EVAL_EQ(L, "return Vector2.Dot(Vector2(3, 4), Vector2(5, 6))", float, 39);
    }

    SECTION("static function") {
        ASSERT_EVAL_EQ(L, "return Vector2.FromAngle(5)", Vector2, Vector2::from_angle(5));
    }

    SECTION("varargs") {
        Ref<PhysicsRayQueryParameters3D> params;
        params.instantiate();

        params->set_collide_with_areas(true);

        LuaStackOp<Callable>::push(L, Callable(params.ptr(), "set"));
        lua_setglobal(L, "testCallable");

        ASSERT_EVAL_OK(L, "testCallable:Call(StringName(\"collide_with_areas\"), false)");
        REQUIRE(!params->is_collide_with_areas_enabled());

        lua_pushnil(L);
        lua_setglobal(L, "testCallable");
    }

    SECTION("default arguments") {
        SECTION("non-vararg") {
            // alpha: default 1.0
            ASSERT_EVAL_EQ(L, "return Color.FromHsv(0.5, 0.5, 0.5)", Color, Color::from_hsv(0.5, 0.5, 0.5, 1.0));
        }

        // no method exists with vararg and default args
    }

    SECTION("non-const") {
        SECTION("non-vararg") {
            PackedStringArray expected;
            expected.push_back("hello");
            expected.push_back("hi");

            ASSERT_EVAL_EQ(L, R"ASDF(
                local array = PackedStringArray()
                array:PushBack("hello")
                array:PushBack("hi")

                return array
            )ASDF",
                    PackedStringArray, expected);
        }

        // all vararg methods are const
    }
}

TEST_CASE_METHOD(LuauFixture, "builtins: setget") {
    SECTION("member access") {
        ASSERT_EVAL_EQ(L, "return Vector2(123, 456).y", float, 456);
    }

    SECTION("index access") {
        ASSERT_EVAL_EQ(L, "return Vector2(123, 456)[2]", float, 456);
    }

    SECTION("member set fails") {
        ASSERT_EVAL_FAIL(L, "Vector2(123, 456).y = 0", "exec:1: type 'Vector2' is read-only");
    }

    SECTION("array index set") {
        PackedStringArray expected;
        expected.push_back("hello");

        ASSERT_EVAL_EQ(L, R"ASDF(
            local array = PackedStringArray()
            array:PushBack("hi there")
            array[1] = "hello"

            return array
        )ASDF",
                PackedStringArray, expected);
    }

    SECTION("keyed access") {
        Dictionary input;
        input[Vector2(1, 2)] = "hi!";

        LuaStackOp<Dictionary>::push(L, input);
        lua_setglobal(L, "testDict");

        ASSERT_EVAL_EQ(L, "return testDict[Vector2(1, 2)]", String, "hi!");

        lua_pushnil(L);
        lua_setglobal(L, "testDict");
    }

    SECTION("keyed set") {
        Dictionary expected;
        expected["one"] = 12.5;

        ASSERT_EVAL_EQ(L, R"ASDF(
            local dictionary = Dictionary()
            dictionary["one"] = 12.5

            return dictionary
        )ASDF",
                Dictionary, expected);
    }
}

TEST_CASE_METHOD(LuauFixture, "builtins: operators") {
    SECTION("equality") {
        ASSERT_EVAL_EQ(L, "return Vector2(1, 2) == Vector2(1, 2)", bool, true);
    }

    SECTION("inequality") {
        ASSERT_EVAL_EQ(L, "return Vector2(1, 2) ~= Vector2(1, 2)", bool, false);
    }

    SECTION("addition") {
        ASSERT_EVAL_EQ(L, "return Vector2(1, 2) + Vector2(3, 4)", Vector2, Vector2(4, 6));
    }

    SECTION("unary -") {
        ASSERT_EVAL_EQ(L, "return -Vector2(1, 2)", Vector2, Vector2(-1, -2));
    }

    SECTION("special case: length") {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local arr = PackedStringArray()
            arr:PushBack("a")
            arr:PushBack("b")
            arr:PushBack("c")
            arr:PushBack("d")
            arr:PushBack("e")

            return #arr
        )ASDF",
                int, 5);
    }
}

TEST_CASE_METHOD(LuauFixture, "builtins: consts and enums") {
    SECTION("constants") {
        ASSERT_EVAL_EQ(L, "return Vector2.ONE", Vector2, Vector2(1, 1));
    }

    SECTION("enums") {
        ASSERT_EVAL_EQ(L, "return Vector3.Axis.Z", int, 2);
    }
}

TEST_CASE_METHOD(LuauFixture, "builtins: tostring") {
    ASSERT_EVAL_EQ(L, "return tostring(Vector3(0, 1, 2))", String, "(0, 1, 2)");
}
