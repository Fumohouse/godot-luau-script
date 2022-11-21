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
        local TestClass = {
            name = "TestClass",
            tool = true,
            methods = {},
            properties = {}
        }

        function TestClass:TestMethod()
        end

        TestClass.methods["TestMethod"] = {}

        function TestClass:__WeirdMethodName()
        end

        TestClass.methods["__WeirdMethodName"] = {}

        TestClass.properties["testProperty"] = {
            property = gdproperty({ name = "testProperty", type = Enum.VariantType.TYPE_FLOAT }),
            default = 5.5
        }

        return TestClass
    )ASDF");

    script->reload();

    REQUIRE(script->_is_valid());

    SECTION("method methods")
    {
        REQUIRE(script->is_tool());
        REQUIRE(script->get_script_method_list().size() == 2);
        REQUIRE(script->_has_method("TestMethod"));
        REQUIRE(script->_get_method_info("TestMethod") == GDMethod({"TestMethod"}).operator Dictionary());

        SECTION("method name conversion")
        {
            SECTION("normal")
            {
                StringName actual_name;
                bool has_method = script->has_method("test_method", &actual_name);

                REQUIRE(has_method);
                REQUIRE(actual_name == StringName("TestMethod"));
            }

            SECTION("leading underscores")
            {
                StringName actual_name;
                bool has_method = script->has_method("__weird_method_name", &actual_name);

                REQUIRE(has_method);
                REQUIRE(actual_name == StringName("__WeirdMethodName"));
            }
        }
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
        local TestClass = {
            name = "TestClass",
            methods = {},
            properties = {}
        }

        local testClassIndex = {}

        function testClassIndex:PrivateMethod()
            return "hi"
        end

        function TestClass._Init(obj, tbl)
            setmetatable(tbl, { __index = testClassIndex })

            tbl.testField = 1
        end

        function TestClass:_Ready()
        end

        TestClass.methods["_Ready"] = {}

        function TestClass:TestMethod(arg1, arg2)
            return string.format("%.1f, %s", arg1, arg2)
        end

        TestClass.methods["TestMethod"] = {
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
        }

        function TestClass:TestMethod2(arg1, arg2)
            return 3.14
        end

        TestClass.methods["TestMethod2"] = {
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
        }

        TestClass.properties["testProperty"] = {
            property = gdproperty({ name = "testProperty", type = Enum.VariantType.TYPE_FLOAT }),
            getter = "GetTestProperty",
            setter = "SetTestProperty",
            default = 5.5
        }

        TestClass.properties["testProperty2"] = {
            property = gdproperty({ name = "testProperty2", type = Enum.VariantType.TYPE_STRING }),
            getter = "GetTestProperty2",
            setter = "SetTestProperty2",
            default = "hey"
        }

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

        // TODO: this is quite bad. order is not guaranteed when iterating in Luau, so these indices are magic based on what the VM did.
        REQUIRE(count == 3);
        REQUIRE(methods[0].return_value.type == GDNATIVE_VARIANT_TYPE_FLOAT);
        REQUIRE(methods[0].argument_count == 2);
        REQUIRE(*((StringName *)methods[0].arguments[1].name) == StringName("arg2"));
        REQUIRE(*((StringName *)methods[2].name) == StringName("TestMethod"));

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

        SECTION("virtual method name conversion")
        {
            Variant args[] = {};
            Variant ret;
            GDNativeCallError err;

            inst.call("_ready", args, 0, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
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

    SECTION("table setget")
    {
        SECTION("normal")
        {
            lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
            int top = lua_gettop(L);

            SECTION("set")
            {
                lua_pushstring(L, "testField");
                lua_pushinteger(L, 2);

                bool set_is_valid = inst.table_set(L);
                REQUIRE(set_is_valid);
                REQUIRE(lua_gettop(L) == top);

                lua_pushstring(L, "testField");

                bool get_is_valid = inst.table_get(L);
                REQUIRE(get_is_valid);
                REQUIRE(lua_tointeger(L, -1) == 2);
            }

            SECTION("get")
            {
                lua_pushstring(L, "PrivateMethod");
                bool is_valid = inst.table_get(L);

                REQUIRE(is_valid);
                REQUIRE(lua_isLfunction(L, -1));
                REQUIRE(lua_gettop(L) == top + 1);
            }
        }

        SECTION("wrong thread")
        {
            lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);

            SECTION("set")
            {
                lua_pushstring(L, "testField");
                lua_pushstring(L, "asdf");

                bool is_valid = inst.table_set(L);
                REQUIRE(!is_valid);
            }

            SECTION("get")
            {
                lua_pushstring(L, "PrivateMethod");

                bool is_valid = inst.table_get(L);
                REQUIRE(!is_valid);
            }
        }
    }
}