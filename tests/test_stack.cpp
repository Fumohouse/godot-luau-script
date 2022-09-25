#include <catch_amalgamated.hpp>

#include <lua.h>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "test_utils.h"

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

using namespace godot;

#define TEST_STACK_OP(type, value)                  \
    LuaStackOp<type>::push(L, value);               \
    REQUIRE(LuaStackOp<type>::get(L, -1) == value); \
    lua_pop(L, 1);

TEST_CASE_METHOD(LuauFixture, "vm: stack operations")
{
    TEST_STACK_OP(bool, true);
    TEST_STACK_OP(int, 12);
    TEST_STACK_OP(String, "hello there! おはようございます");
    TEST_STACK_OP(Transform3D, Transform3D().rotated(Vector3(1, 1, 1).normalized(), 2));
}
