#include <catch_amalgamated.hpp>

#include <godot/gdnative_interface.h>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/memory.hpp>

#include "luau_script.h"
#include "gd_luau.h"
#include "luau_lib.h"

TEST_CASE("luau script: script load")
{
    // TODO: singleton is not available during test runs.
    // this will construct the singleton then destroy it at the end of the scope.
    GDLuau gd_luau;

    Ref<LuauScript> script;
    script.instantiate();

    script->set_source_code(R"ASDF(
        local TestClass = gdclass("TestClass")

        TestClass:Tool(true)

        TestClass:RegisterMethod("TestMethod", function() end)
        TestClass:RegisterProperty(gdproperty({ name = "testProperty", type = Enum.VariantType.TYPE_FLOAT }), "GetTestProperty", "SetTestProperty", 5.5)

        return TestClass
    )ASDF");

    script->reload();

    REQUIRE(script->_is_valid());

    SECTION("method methods")
    {
        REQUIRE(script->is_tool());
        REQUIRE(script->get_script_method_list().size() == 1);
        REQUIRE(script->_has_method("TestMethod"));
        REQUIRE(script->_get_method_info("TestMethod") == GDMethod().operator Dictionary());
    }

    SECTION("property methods")
    {
        REQUIRE(script->get_script_property_list().size() == 1);
        // test fails without cast. don't know why
        REQUIRE(script->_get_members()[0] == StringName("testProperty"));
        REQUIRE(script->_has_property_default_value("testProperty"));
        REQUIRE(script->_get_property_default_value("testProperty") == Variant(5.5));
    }
}

TEST_CASE("luau script: instance")
{
    GDLuau gd_luau;

    Ref<LuauScript> script;
    script.instantiate();

    script->set_source_code(R"ASDF(
        local TestClass = gdclass("TestClass")

        TestClass:RegisterMethod("TestMethod", function(self, arg1, arg2)
            return string.format("%.1f, %s", arg1, arg2)
        end, {
            args = {
                gdproperty({
                    name = "arg1",
                    type = Enum.VariantType.TYPE_FLOAT
                }),
                gdproperty({
                    name = "arg2",
                    type = Enum.VariantType.TYPE_STRING
                })
            },
            defaultArgs = { "hi" },
            returnVal = gdproperty({ type = Enum.VariantType.TYPE_STRING })
        })

        TestClass:RegisterMethod("TestMethod2", function(self, arg1, arg2)
            return 3.14
        end, {
            args = {
                gdproperty({
                    name = "arg1",
                    type = Enum.VariantType.TYPE_STRING
                }),
                gdproperty({
                    name = "arg2",
                    type = Enum.VariantType.TYPE_INT
                })
            },
            defaultArgs = { "godot", 1 },
            returnVal = gdproperty({ type = Enum.VariantType.TYPE_FLOAT })
        })

        TestClass:RegisterProperty(gdproperty({ name = "testProperty", type = Enum.VariantType.TYPE_FLOAT }), "GetTestProperty", "SetTestProperty", 5.5)
        TestClass:RegisterProperty(gdproperty({ name = "testProperty2", type = Enum.VariantType.TYPE_STRING }), "GetTestProperty2", "SetTestProperty2", "hey")

        return TestClass
    )ASDF");

    script->reload();

    REQUIRE(script->_is_valid());

    Object obj;
    LuauScriptInstance inst(script, &obj, GDLuau::VM_CORE);

    REQUIRE(inst.get_owner() == &obj);
    REQUIRE(inst.get_script() == script);

    SECTION("method methods")
    {
        REQUIRE(inst.has_method("TestMethod"));
        REQUIRE(inst.has_method("TestMethod2"));

        uint32_t count;
        GDNativeMethodInfo *methods = inst.get_method_list(&count);

        REQUIRE(count == 2);
        REQUIRE(*((StringName *)methods[0].name) == StringName("TestMethod"));
        REQUIRE(methods[1].return_value.type == GDNATIVE_VARIANT_TYPE_FLOAT);
        REQUIRE(methods[1].argument_count == 2);
        REQUIRE(*((StringName *)methods[1].arguments[1].name) == StringName("arg2"));

        inst.free_method_list(methods);
    }

    SECTION("property methods")
    {
        bool is_valid;
        Variant::Type type = inst.get_property_type("testProperty", &is_valid);

        REQUIRE(is_valid);
        REQUIRE(type == Variant::Type::FLOAT);

        uint32_t count;
        GDNativePropertyInfo *properties = inst.get_property_list(&count);

        REQUIRE(count == 2);
        REQUIRE(*((StringName *)properties[0].name) == StringName("testProperty"));
        REQUIRE(properties[0].type == GDNATIVE_VARIANT_TYPE_FLOAT);
        REQUIRE(*((StringName *)properties[1].name) == StringName("testProperty2"));
        REQUIRE(properties[1].type == GDNATIVE_VARIANT_TYPE_STRING);

        inst.free_property_list(properties);
    }

    SECTION("call")
    {
        SECTION("normal operation")
        {
            Variant args[] = {2.5f, "Hello world"};
            Variant ret;
            GDNativeCallError err;

            inst.call("TestMethod", args, 2, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
            REQUIRE(ret == "2.5, Hello world");
        }

        SECTION("default argument")
        {
            Variant args[] = {5.3f};
            Variant ret;
            GDNativeCallError err;

            inst.call("TestMethod", args, 1, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
            REQUIRE(ret == "5.3, hi");
        }

        SECTION("invalid arguments")
        {
            SECTION("too few")
            {
                Variant args[] = {};
                Variant ret;
                GDNativeCallError err;

                inst.call("TestMethod", args, 0, &ret, &err);

                REQUIRE(err.error == GDNATIVE_CALL_ERROR_TOO_FEW_ARGUMENTS);
                REQUIRE(err.argument == 1);
            }

            SECTION("too many")
            {
                Variant args[] = {1, 1, 1, 1, 1};
                Variant ret;
                GDNativeCallError err;

                inst.call("TestMethod", args, 5, &ret, &err);

                REQUIRE(err.error == GDNATIVE_CALL_ERROR_TOO_MANY_ARGUMENTS);
                REQUIRE(err.argument == 2);
            }

            SECTION("invalid")
            {
                Variant args[] = {false};
                Variant ret;
                GDNativeCallError err;

                inst.call("TestMethod", args, 1, &ret, &err);

                REQUIRE(err.error == GDNATIVE_CALL_ERROR_INVALID_ARGUMENT);
                REQUIRE(err.argument == 0);
                REQUIRE(err.expected == GDNATIVE_VARIANT_TYPE_FLOAT);
            }
        }
    }
}