#include <catch_amalgamated.hpp>

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "luagd.h"
#include "luagd_permissions.h"

#include "test_utils.h"

using namespace godot;

TEST_CASE_METHOD(LuauFixture, "classes: reference counting")
{
    Ref<PhysicsRayQueryParameters3D> params;
    params.instantiate();

    LuaStackOp<PhysicsRayQueryParameters3D *>::push(L, params.ptr());
    params->unreference();

    REQUIRE(UtilityFunctions::is_instance_valid(params));

    lua_pop(L, 1);
    lua_gc(L, LUA_GCCOLLECT, 0);

    REQUIRE(!UtilityFunctions::is_instance_valid(params));
}

TEST_CASE_METHOD(LuauFixture, "classes: constructor")
{
    ASSERT_EVAL_EQ(L, "return PhysicsRayQueryParameters3D():GetClass()", String, "PhysicsRayQueryParameters3D");
}

TEST_CASE_METHOD(LuauFixture, "classes: singleton getter")
{
    ASSERT_EVAL_EQ(L, "return Engine.GetSingleton()", Object *, Engine::get_singleton());
}

TEST_CASE_METHOD(LuauFixture, "classes: consts and enums")
{
    SECTION("constants")
    {
        ASSERT_EVAL_EQ(L, "return Object.NOTIFICATION_PREDELETE", int, 1);
    }

    SECTION("enums")
    {
        ASSERT_EVAL_EQ(L, "return Object.ConnectFlags.CONNECT_ONE_SHOT", int, 4)
    }
}

TEST_CASE_METHOD(LuauFixture, "classes: methods/functions")
{
    GDThreadData *udata = luaGD_getthreaddata(L);
    udata->permissions = PERMISSION_INTERNAL;

    SECTION("namecall style")
    {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            return params:Get(StringName("collide_with_areas"))
        )ASDF", bool, false);
    }

    SECTION("invoked from global table")
    {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            return Object.Get(params, StringName("collide_with_areas"))
        )ASDF", bool, false);
    }

    SECTION("varargs")
    {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            params:Call(StringName("set"), StringName("collide_with_areas"), true)

            return params.collideWithAreas
        )ASDF", bool, true);
    }

    udata->permissions = PERMISSION_BASE;
}

TEST_CASE_METHOD(LuauFixture, "classes: setget")
{
    SECTION("member access")
    {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local node = PhysicsRayQueryParameters3D()
            return node.collideWithAreas
        )ASDF", bool, false);
    }

    SECTION("member set")
    {
        ASSERT_EVAL_EQ(L, R"ASDF(
            local node = PhysicsRayQueryParameters3D()
            node.collideWithAreas = true
            return node.collideWithAreas
        )ASDF", bool, true);
    }
}

TEST_CASE_METHOD(LuauFixture, "classes: tostring")
{
    Node3D *node = memnew(Node3D);

    LuaStackOp<Node3D *>::push(L, node);
    lua_setglobal(L, "node");

    SECTION("normal")
    {
        ASSERT_EVAL_EQ(L, "return tostring(node)", String, node->to_string());
    }

    memdelete(node);

    SECTION("freed")
    {
        ASSERT_EVAL_EQ(L, "return tostring(node)", String, "<Freed Object>");
    }

    lua_pushnil(L);
    lua_setglobal(L, "node");
}

TEST_CASE_METHOD(LuauFixture, "classes: permissions")
{
    ASSERT_EVAL_FAIL(
        L,
        "OS.GetSingleton():GetName()",
        "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access OS.GetName. needed permissions: 2, got: 0 !!!");
}
