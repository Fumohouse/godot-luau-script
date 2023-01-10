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
            return gdproperty({
                type = Enum.VariantType.OBJECT,
                name = "testProp",
                hint = Enum.PropertyHint.ENUM,
                hintString = "val1,val2,val3",
                usage = Enum.PropertyUsageFlags.NONE,
                className = "Node3D"
            })
        )ASDF",
                {
                    GDProperty *prop = LuaStackOp<GDProperty>::check_ptr(L, -1);
                    REQUIRE(prop->operator Dictionary() == expected.operator Dictionary());
                })
    }

    SECTION("some properties") {
        GDProperty expected;

        expected.type = GDEXTENSION_VARIANT_TYPE_OBJECT;
        expected.name = "testProp";
        expected.class_name = "Node2D";

        EVAL_THEN(L, R"ASDF(
            return gdproperty({
                type = Enum.VariantType.OBJECT,
                name = "testProp",
                className = "Node2D"
            })
        )ASDF",
                {
                    GDProperty *prop = LuaStackOp<GDProperty>::check_ptr(L, -1);
                    REQUIRE(prop->operator Dictionary() == expected.operator Dictionary());
                })
    }
}

TEST_CASE_METHOD(LuauFixture, "lib: classes") {
    luascript_openlibs(L);

    lua_State *T = lua_newthread(L);

    SECTION("explicit extends"){
        EVAL_THEN(T, "return { name = 'TestClass', extends = 'Node3D' }", {
            GDClassDefinition def = luascript_read_class(T, -1);
            REQUIRE(def.name == "TestClass");
            REQUIRE(def.extends == "Node3D");
        })
    }

    SECTION("default extends"){ EVAL_THEN(T, "return { name = 'TestClass' }", {
        GDClassDefinition def = luascript_read_class(T, -1);
        REQUIRE(def.name == "TestClass");
        REQUIRE(def.extends == "RefCounted");
    }) }

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
            return {
                name = "TestClass",
                extends = "Node3D",
                tool = true,
                methods = {
                    TestMethod = {
                        args = {
                            gdproperty({
                                name = "arg1",
                                type = Enum.VariantType.FLOAT
                            }),
                            gdproperty({
                                name = "arg2",
                                type = Enum.VariantType.STRING
                            })
                        },
                        defaultArgs = { 1, "godot" },
                        returnVal = gdproperty({ type = Enum.VariantType.STRING }),
                        flags = Enum.MethodFlags.NORMAL
                    }
                },
                properties = {
                    testProperty = {
                        property = gdproperty({ name = "testProperty", type = Enum.VariantType.FLOAT }),
                        getter = "GetTestProperty",
                        setter = "SetTestProperty",
                        default = 3.5
                    }
                },
                signals = {
                    testSignal = {
                        args = {
                            gdproperty({ name = "arg1", type = Enum.VariantType.FLOAT })
                        }
                    }
                },
                rpcs = {
                    testRpc = {
                        rpcMode = MultiplayerAPI.RPCMode.ANY_PEER,
                        transferMode = MultiplayerPeer.TransferMode.RELIABLE,
                        callLocal = true,
                        channel = 4
                    }
                },
                constants = {
                    TEST_CONSTANT = Vector2(1, 2)
                }
            }
        )ASDF",
                {
                    GDClassDefinition def = luascript_read_class(T, -1);

                    SECTION("methods") {
                        REQUIRE(def.methods.has("TestMethod"));
                        REQUIRE(def.methods.get("TestMethod").operator Dictionary() == expected_method.operator Dictionary());
                    }

                    SECTION("properties") {
                        REQUIRE(def.properties.has("testProperty"));

                        const GDClassProperty &prop = def.properties.get("testProperty");
                        REQUIRE(prop.property.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
                        REQUIRE(prop.property.operator Dictionary() == expected_property.operator Dictionary());
                        REQUIRE(prop.getter == StringName("GetTestProperty"));
                        REQUIRE(prop.setter == StringName("SetTestProperty"));
                        REQUIRE(prop.default_value == Variant(3.5));
                    }

                    SECTION("signals") {
                        REQUIRE(def.signals.has("testSignal"));

                        const GDMethod &signal = def.signals.get("testSignal");
                        REQUIRE(signal.arguments.size() == 1);

                        const GDProperty &arg = signal.arguments[0];
                        REQUIRE(arg.name == "arg1");
                        REQUIRE(arg.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
                    }

                    SECTION("rpcs") {
                        REQUIRE(def.rpcs.has("testRpc"));

                        const GDRpc &rpc = def.rpcs.get("testRpc");
                        REQUIRE(rpc.name == "testRpc");
                        REQUIRE(rpc.rpc_mode == MultiplayerAPI::RPC_MODE_ANY_PEER);
                        REQUIRE(rpc.transfer_mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE);
                        REQUIRE(rpc.call_local);
                        REQUIRE(rpc.channel == 4);
                    }

                    SECTION("constants") {
                        REQUIRE(def.constants.has("TEST_CONSTANT"));
                        REQUIRE(def.constants["TEST_CONSTANT"] == Vector2(1, 2));
                    }
                })
    }

    lua_pop(L, 1);
}
