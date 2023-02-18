#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <lua.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_stack.h"
#include "luau_lib.h"
#include "test_utils.h"

using namespace godot;

TEST_CASE_METHOD(LuauFixture, "lib: gdproperty") {
    luascript_openlibs(L);

    SECTION("all properties") {
        GDProperty expected;

        expected.type = GDEXTENSION_VARIANT_TYPE_OBJECT;
        expected.name = "testProp";
        expected.hint = PropertyHint::PROPERTY_HINT_ENUM;
        expected.hint_string = "val1,val2,val3";
        expected.usage = PropertyUsageFlags::PROPERTY_USAGE_NONE;
        expected.class_name = "Node3D";

        EVAL_THEN(L, R"ASDF(
            return {
                type = Enum.VariantType.OBJECT,
                name = "testProp",
                hint = Enum.PropertyHint.ENUM,
                hintString = "val1,val2,val3",
                usage = Enum.PropertyUsageFlags.NONE,
                className = "Node3D"
            }
        )ASDF",
                {
                    GDProperty prop = luascript_read_property(L, -1);
                    REQUIRE(prop.operator Dictionary() == expected.operator Dictionary());
                })
    }

    SECTION("some properties") {
        GDProperty expected;

        expected.type = GDEXTENSION_VARIANT_TYPE_OBJECT;
        expected.name = "testProp";
        expected.class_name = "Node2D";

        EVAL_THEN(L, R"ASDF(
            return {
                type = Enum.VariantType.OBJECT,
                name = "testProp",
                className = "Node2D"
            }
        )ASDF",
                {
                    GDProperty prop = luascript_read_property(L, -1);
                    REQUIRE(prop.operator Dictionary() == expected.operator Dictionary());
                })
    }
}

TEST_CASE_METHOD(LuauFixture, "lib: classes") {
    luascript_openlibs(L);

    lua_State *T = lua_newthread(L);

    SECTION("explicit extends") {
        EVAL_THEN(T, "return gdclass('TestClass', Node3D)", {
            GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, -1);
            REQUIRE(def->name == "TestClass");
            REQUIRE(def->extends == "Node3D");
        })
    }

    SECTION("default extends") {
        EVAL_THEN(T, "return gdclass('TestClass')", {
            GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, -1);
            REQUIRE(def->name == "TestClass");
            REQUIRE(def->extends == "RefCounted");
        })
    }

    SECTION("get from definition table") {
        EVAL_THEN(T, R"ASDF(
            local TestClass = gdclass(nil, Node)
            function TestClass:TestMethod() end

            return TestClass
        )ASDF",
                {
                    lua_getfield(T, -1, "TestMethod");
                    REQUIRE(lua_type(T, -1) == LUA_TFUNCTION);
                })
    }

    SECTION("default property value type checks") {
        SECTION("non-nil") {
            ASSERT_EVAL_FAIL(T, R"ASDF(
                local TestClass = gdclass()
                TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT):Default("what?")
            )ASDF",
                    "exec:3: invalid argument #2 to 'Luau.GDClassProperty.__namecall' (float expected, got string)")
        }

        SECTION("nil") {
            ASSERT_EVAL_FAIL(T, R"ASDF(
                local TestClass = gdclass()
                TestClass:RegisterProperty("testProperty"):Default("what?")
            )ASDF",
                    "exec:3: invalid argument #2 to 'Luau.GDClassProperty.__namecall' (nil expected, got string)")
        }
    }

    SECTION("full example") {
        GDMethod expected_method;

        {
            expected_method.name = "TestMethod";

            GDProperty expected_arg1;
            expected_arg1.name = "arg1";
            expected_arg1.type = GDEXTENSION_VARIANT_TYPE_FLOAT;

            GDProperty expected_arg2;
            expected_arg2.name = "arg2";
            expected_arg2.type = GDEXTENSION_VARIANT_TYPE_STRING;

            expected_method.arguments.push_back(expected_arg1);
            expected_method.arguments.push_back(expected_arg2);

            expected_method.default_arguments.push_back(1);
            expected_method.default_arguments.push_back("godot");

            GDProperty expected_ret;
            expected_ret.type = GDEXTENSION_VARIANT_TYPE_STRING;

            expected_method.return_val = expected_ret;

            expected_method.flags = MethodFlags::METHOD_FLAG_NORMAL;
        }

        GDProperty expected_property;
        expected_property.name = "testProperty";
        expected_property.type = GDEXTENSION_VARIANT_TYPE_FLOAT;

        EVAL_THEN(T, R"ASDF(
            local TestClass = gdclass("TestClass", Node3D)
                :Tool(true)

            TestClass:RegisterMethod("TestMethod")
                :Args(
                    {
                        name = "arg1",
                        type = Enum.VariantType.FLOAT
                    },
                    {
                        name = "arg2",
                        type = Enum.VariantType.STRING
                    }
                )
                :DefaultArgs(1, "godot")
                :ReturnVal({ type = Enum.VariantType.STRING })
                :Flags(Enum.MethodFlags.NORMAL)

            TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
                :SetGet("SetTestProperty", "GetTestProperty")
                :Default(3.5)

            TestClass:RegisterSignal("testSignal")
                :Args(
                    { name = "arg1", type = Enum.VariantType.FLOAT }
                )

            TestClass:RegisterRpc("TestRpc", {
                rpcMode = MultiplayerAPI.RPCMode.ANY_PEER,
                transferMode = MultiplayerPeer.TransferMode.RELIABLE,
                callLocal = true,
                channel = 4
            })

            TestClass:RegisterConstant("TEST_CONSTANT", Vector2.new(1, 2))

            return TestClass
        )ASDF",
                {
                    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::get_ptr(T, -1);

                    SECTION("methods") {
                        REQUIRE(def->methods.has("TestMethod"));
                        REQUIRE(def->methods.get("TestMethod").operator Dictionary() == expected_method.operator Dictionary());
                    }

                    SECTION("properties") {
                        REQUIRE(def->property_indices.has("testProperty"));

                        const GDClassProperty &prop = def->properties[def->property_indices["testProperty"]];
                        REQUIRE(prop.property.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
                        REQUIRE(prop.property.operator Dictionary() == expected_property.operator Dictionary());
                        REQUIRE(prop.getter == StringName("GetTestProperty"));
                        REQUIRE(prop.setter == StringName("SetTestProperty"));
                        REQUIRE(prop.default_value == Variant(3.5));
                    }

                    SECTION("signals") {
                        REQUIRE(def->signals.has("testSignal"));

                        const GDMethod &signal = def->signals.get("testSignal");
                        REQUIRE(signal.arguments.size() == 1);

                        const GDProperty &arg = signal.arguments[0];
                        REQUIRE(arg.name == "arg1");
                        REQUIRE(arg.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
                    }

                    SECTION("rpcs") {
                        REQUIRE(def->rpcs.has("TestRpc"));

                        const GDRpc &rpc = def->rpcs.get("TestRpc");
                        REQUIRE(rpc.name == "TestRpc");
                        REQUIRE(rpc.rpc_mode == MultiplayerAPI::RPC_MODE_ANY_PEER);
                        REQUIRE(rpc.transfer_mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE);
                        REQUIRE(rpc.call_local);
                        REQUIRE(rpc.channel == 4);
                    }

                    SECTION("constants") {
                        REQUIRE(def->constants.has("TEST_CONSTANT"));
                        REQUIRE(def->constants["TEST_CONSTANT"] == Vector2(1, 2));
                    }
                })
    }

    lua_pop(L, 1);
}
