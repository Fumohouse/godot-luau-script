#include <catch_amalgamated.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/style_box.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "lua.h"
#include "luagd.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"

#include "test_utils.h"

using namespace godot;

TEST_CASE_METHOD(LuauFixture, "classes: reference counting") {
    Ref<PhysicsRayQueryParameters3D> params;
    params.instantiate();

    LuaStackOp<Object *>::push(L, params.ptr());
    params->unreference();

    REQUIRE(UtilityFunctions::is_instance_valid(params));

    lua_pop(L, 1);
    lua_gc(L, LUA_GCCOLLECT, 0);

    REQUIRE(!UtilityFunctions::is_instance_valid(params));
}

TEST_CASE_METHOD(LuauFixture, "classes: constructor"){
    ASSERT_EVAL_EQ(L, "return PhysicsRayQueryParameters3D():GetClass()", String, "PhysicsRayQueryParameters3D")
}

TEST_CASE_METHOD(LuauFixture, "classes: singleton getter"){
    ASSERT_EVAL_EQ(L, "return Engine.GetSingleton()", Object *, Engine::get_singleton())
}

TEST_CASE_METHOD(LuauFixture, "classes: consts and enums") {
    SECTION("constants"){
        ASSERT_EVAL_EQ(L, "return Object.NOTIFICATION_PREDELETE", int, 1)
    }

    SECTION("enums") {
        ASSERT_EVAL_EQ(L, "return Object.ConnectFlags.CONNECT_ONE_SHOT", int, 4)
    }
}

TEST_CASE_METHOD(LuauFixture, "classes: methods/functions") {
    GDThreadData *udata = luaGD_getthreaddata(L);
    udata->permissions = PERMISSION_INTERNAL;

    SECTION("namecall style"){
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            return params:Get(StringName("collide_with_areas"))
        )ASDF",
                bool, false)
    }

    SECTION("invoked from global table"){
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            return Object.Get(params, StringName("collide_with_areas"))
        )ASDF",
                bool, false)
    }

    SECTION("varargs"){
        ASSERT_EVAL_EQ(L, R"ASDF(
            local params = PhysicsRayQueryParameters3D()
            params:Call(StringName("set"), StringName("collide_with_areas"), true)

            return params.collideWithAreas
        )ASDF",
                bool, true)
    }

    SECTION("default args"){
        EVAL_THEN(L, R"ASDF(
            return PhysicsRayQueryParameters3D.Create(Vector3(1, 2, 3), Vector3(4, 5, 6))
        )ASDF",
                {
                    Ref<PhysicsRayQueryParameters3D> params = Object::cast_to<PhysicsRayQueryParameters3D>(
                            LuaStackOp<Object *>::check(L, -1));

                    REQUIRE(params.is_valid());
                    REQUIRE(params->get_from() == Vector3(1, 2, 3));
                    REQUIRE(params->get_to() == Vector3(4, 5, 6));
                    REQUIRE(params->get_collision_mask() == 4294967295);
                    REQUIRE(params->get_exclude().size() == 0);
                })
    }

    SECTION("ref return"){
        EVAL_THEN(L, R"ASDF(
            return PhysicsRayQueryParameters3D.Create(Vector3(1, 2, 3), Vector3(4, 5, 6), 1, Array())
        )ASDF",
                {
                    RefCounted *rc = Object::cast_to<RefCounted>(LuaStackOp<Object *>::check(L, -1));

                    REQUIRE(UtilityFunctions::is_instance_valid(rc));

                    lua_pop(L, 1);
                    lua_gc(L, LUA_GCCOLLECT, 0);

                    REQUIRE(!UtilityFunctions::is_instance_valid(rc));
                })
    }

    udata->permissions = PERMISSION_BASE;
}

TEST_CASE_METHOD(LuauFixture, "classes: setget") {
    SECTION("signal") {
        Node3D node;

        LuaStackOp<Object *>::push(L, &node);
        lua_setglobal(L, "testNode");

        SECTION("get") {
            ASSERT_EVAL_EQ(L, "return testNode.visibilityChanged", Signal, Signal(&node, "visibility_changed"));
        }

        SECTION("set disallowed") {
            ASSERT_EVAL_FAIL(L, "testNode.visibilityChanged = 1234", "exec:1: cannot assign to signal 'visibilityChanged'");
        }

        lua_pushnil(L);
        lua_setglobal(L, "testNode");
    }

    SECTION("method callable") {
        Object obj;

        LuaStackOp<Object *>::push(L, &obj);
        lua_setglobal(L, "testObject");

        SECTION("get") {
            ASSERT_EVAL_EQ(L, "return testObject.GetClass", Callable, Callable(&obj, "get_class"));
        }

        SECTION("get permissions") {
            ASSERT_EVAL_FAIL(L, "return testObject.Call", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'Godot.Object.Object.Call'. needed permissions: 1, got: 0 !!!");
        }

        SECTION("set disallowed") {
            ASSERT_EVAL_FAIL(L, "testObject.GetClass = 1234", "exec:1: cannot assign to method 'GetClass'");
        }

        lua_pushnil(L);
        lua_setglobal(L, "testObject");
    }

    SECTION("member access"){
        ASSERT_EVAL_EQ(L, R"ASDF(
            local node = PhysicsRayQueryParameters3D()
            return node.collideWithAreas
        )ASDF",
                bool, false)
    }

    SECTION("member set"){
        ASSERT_EVAL_EQ(L, R"ASDF(
            local node = PhysicsRayQueryParameters3D()
            node.collideWithAreas = true
            return node.collideWithAreas
        )ASDF",
                bool, true)
    }

    SECTION("member access with index") {
        Ref<StyleBox> style_box;
        style_box.instantiate();

        style_box->set_default_margin(Side::SIDE_BOTTOM, 4.25);

        LuaStackOp<Object *>::push(L, style_box.ptr());
        lua_setglobal(L, "styleBox");

        ASSERT_EVAL_EQ(L, R"ASDF(
            return styleBox.contentMarginBottom
        )ASDF",
                double, 4.25)

        lua_pushnil(L);
        lua_setglobal(L, "styleBox");
    }
}

TEST_CASE_METHOD(LuauFixture, "classes: tostring") {
    Node3D *node = memnew(Node3D);

    LuaStackOp<Object *>::push(L, node);
    lua_setglobal(L, "node");

    SECTION("normal"){
        ASSERT_EVAL_EQ(L, "return tostring(node)", String, node->to_string())
    }

    memdelete(node);

    SECTION("freed"){
        ASSERT_EVAL_EQ(L, "return tostring(node)", String, "<Freed Object>")
    }

    lua_pushnil(L);
    lua_setglobal(L, "node");
}

TEST_CASE_METHOD(LuauFixture, "classes: permissions"){
    ASSERT_EVAL_FAIL(
            L,
            "OS.GetSingleton():GetName()",
            "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'Godot.Object.OS.GetName'. needed permissions: 2, got: 0 !!!")
}

TEST_CASE_METHOD(LuauFixture, "classes: invalid global access") {
    ASSERT_EVAL_FAIL(L, "return Object.duhduhduh", "exec:1: 'duhduhduh' is not a valid member of 'Object'")
}
