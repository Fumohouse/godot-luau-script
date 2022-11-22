#include <catch_amalgamated.hpp>

#include <godot/gdnative_interface.h>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/memory.hpp>

#include <lua.h>

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
            return "hi there"
        end

        function TestClass._Init(obj, tbl)
            setmetatable(tbl, { __index = testClassIndex })

            tbl.testField = 1
        end

        function TestClass:_Ready()
        end

        TestClass.methods["_Ready"] = {}

        function TestClass:_Notification(what)
            assert(what == 42)
        end

        TestClass.methods["_Notification"] = {}

        function TestClass:_ToString()
            return "my awesome class"
        end

        TestClass.methods["_ToString"] = {}

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

        function TestClass:TestMethod3()
            return self:TestMethod(self:TestMethod2("", 2), self:PrivateMethod())
        end

        TestClass.methods["TestMethod3"] = {
            returnVal = gdproperty({ type = Enum.VariantType.TYPE_STRING })
        }

        function TestClass:GetTestProperty()
            return 6.5
        end

        function TestClass:SetTestProperty(val)
        end

        TestClass.properties["testProperty"] = {
            property = gdproperty({ name = "testProperty", type = Enum.VariantType.TYPE_FLOAT }),
            getter = "GetTestProperty",
            setter = "SetTestProperty",
            default = 5.5
        }

        function TestClass:GetTestProperty2()
            return "hello"
        end

        TestClass.properties["testProperty2"] = {
            property = gdproperty({ name = "testProperty2", type = Enum.VariantType.TYPE_STRING }),
            getter = "GetTestProperty2",
            default = "hey"
        }

        function TestClass:SetTestProperty3(val)
        end

        TestClass.properties["testProperty3"] = {
            property = gdproperty({ name = "testProperty3", type = Enum.VariantType.TYPE_STRING }),
            setter = "SetTestProperty3",
            default = "hey"
        }

        TestClass.properties["testProperty4"] = {
            property = gdproperty({ name = "testProperty4", type = Enum.VariantType.TYPE_STRING }),
            default = "hey"
        }

        return TestClass
    )ASDF");

    script->reload();

    REQUIRE(script->_is_valid());

    Object obj;
    obj.set_script(script);

    LuauScriptInstance *inst = script->instance_get(&obj);

    REQUIRE(inst->get_owner()->get_instance_id() == obj.get_instance_id());
    REQUIRE(inst->get_script() == script);

    SECTION("method methods")
    {
        REQUIRE(inst->has_method("TestMethod"));
        REQUIRE(inst->has_method("TestMethod2"));

        uint32_t count;
        GDNativeMethodInfo *methods = inst->get_method_list(&count);

        REQUIRE(count == 6);

        bool m1_found = false;
        bool m2_found = false;

        for (int i = 0; i < count; i++)
        {
            StringName *name = (StringName *)methods[i].name;

            if (*name == StringName("TestMethod"))
                m1_found = true;
            else if (*name == StringName("TestMethod2"))
            {
                m2_found = true;

                REQUIRE(methods[i].return_value.type == GDNATIVE_VARIANT_TYPE_FLOAT);
                REQUIRE(methods[i].argument_count == 2);
                REQUIRE(*((StringName *)methods[i].arguments[1].name) == StringName("arg2"));
            }
        }

        REQUIRE(m1_found);
        REQUIRE(m2_found);

        inst->free_method_list(methods);
    }

    SECTION("property methods")
    {
        bool is_valid;
        Variant::Type type = inst->get_property_type("testProperty", &is_valid);

        REQUIRE(is_valid);
        REQUIRE(type == Variant::Type::FLOAT);

        uint32_t count;
        GDNativePropertyInfo *properties = inst->get_property_list(&count);

        REQUIRE(count == 4);

        bool p1_found = false;
        bool p2_found = false;

        for (int i = 0; i < count; i++)
        {
            StringName *name = (StringName *)properties[i].name;

            if (*name == StringName("testProperty"))
            {
                p1_found = true;
                REQUIRE(properties[i].type == GDNATIVE_VARIANT_TYPE_FLOAT);
            }
            else if (*name == StringName("testProperty2"))
            {
                p2_found = true;
                REQUIRE(properties[i].type == GDNATIVE_VARIANT_TYPE_STRING);
            }
        }

        REQUIRE(p1_found);
        REQUIRE(p2_found);

        inst->free_property_list(properties);
    }

    SECTION("call")
    {
        SECTION("normal operation")
        {
            Variant args[] = {2.5f, "Hello world"};
            Variant ret;
            GDNativeCallError err;

            inst->call("TestMethod", args, 2, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
            REQUIRE(ret == "2.5, Hello world");
        }

        SECTION("virtual method name conversion")
        {
            Variant args[] = {};
            Variant ret;
            GDNativeCallError err;

            inst->call("_ready", args, 0, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
        }

        SECTION("default argument")
        {
            Variant args[] = {5.3f};
            Variant ret;
            GDNativeCallError err;

            inst->call("TestMethod", args, 1, &ret, &err);

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

                inst->call("TestMethod", args, 0, &ret, &err);

                REQUIRE(err.error == GDNATIVE_CALL_ERROR_TOO_FEW_ARGUMENTS);
                REQUIRE(err.argument == 1);
            }

            SECTION("too many")
            {
                Variant args[] = {1, 1, 1, 1, 1};
                Variant ret;
                GDNativeCallError err;

                inst->call("TestMethod", args, 5, &ret, &err);

                REQUIRE(err.error == GDNATIVE_CALL_ERROR_TOO_MANY_ARGUMENTS);
                REQUIRE(err.argument == 2);
            }

            SECTION("invalid")
            {
                Variant args[] = {false};
                Variant ret;
                GDNativeCallError err;

                inst->call("TestMethod", args, 1, &ret, &err);

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

                bool set_is_valid = inst->table_set(L);
                REQUIRE(set_is_valid);
                REQUIRE(lua_gettop(L) == top);

                lua_pushstring(L, "testField");

                bool get_is_valid = inst->table_get(L);
                REQUIRE(get_is_valid);
                REQUIRE(lua_tointeger(L, -1) == 2);
            }

            SECTION("get")
            {
                lua_pushstring(L, "PrivateMethod");
                bool is_valid = inst->table_get(L);

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

                bool is_valid = inst->table_set(L);
                REQUIRE(!is_valid);
            }

            SECTION("get")
            {
                lua_pushstring(L, "PrivateMethod");

                bool is_valid = inst->table_get(L);
                REQUIRE(!is_valid);
            }
        }
    }

    SECTION("notification")
    {
        int status;
        inst->notification(42, &status);

        REQUIRE(status == LUA_OK);
    }

    SECTION("to string")
    {
        bool is_valid;
        String out;
        inst->to_string((GDNativeBool *)&is_valid, &out);

        REQUIRE(is_valid);
        REQUIRE(out == "my awesome class");
    }

    SECTION("setget")
    {
        SECTION("set")
        {
            SECTION("with getter and setter")
            {
                bool is_valid = inst->set("testProperty", 3.5);
                REQUIRE(is_valid);
            }

            SECTION("with wrong type")
            {
                bool is_valid = inst->set("testProperty", "asdf");
                REQUIRE(!is_valid);
            }

            SECTION("read only")
            {
                bool is_valid = inst->set("testProperty2", "hey there");
                REQUIRE(!is_valid);
            }
        }

        SECTION("get")
        {
            SECTION("with getter and setter")
            {
                Variant val;
                bool is_valid = inst->get("testProperty", val);

                REQUIRE(is_valid);
                REQUIRE(val == Variant(6.5));
            }

            SECTION("write only")
            {
                Variant val;
                bool is_valid = inst->get("testProperty3", val);

                REQUIRE(!is_valid);
            }
        }

        SECTION("with no getter or setter")
        {
            bool set_is_valid = inst->set("testProperty4", "asdf");
            REQUIRE(set_is_valid);

            Variant new_val;
            bool get_is_valid = inst->get("testProperty4", new_val);

            REQUIRE(get_is_valid);
            REQUIRE(new_val == "asdf");
        }

        SECTION("property state")
        {
            GDNativeExtensionScriptInstancePropertyStateAdd add = [](const GDNativeStringNamePtr p_name, const GDNativeVariantPtr p_value, void *p_userdata)
            {
                ((HashMap<StringName, Variant> *)p_userdata)->insert(*((const StringName *)p_name), *((const Variant *)p_value));
            };

            HashMap<StringName, Variant> state;

            inst->get_property_state(add, &state);

            REQUIRE(state.size() == 3);
            REQUIRE(state["testProperty"] == Variant(6.5));
            REQUIRE(state["testProperty2"] == "hello");
            REQUIRE(state["testProperty4"] == Variant());
        }
    }

    SECTION("metatable")
    {
        SECTION("namecall")
        {
            Variant args[] = {};
            Variant ret;
            GDNativeCallError err;
            inst->call("TestMethod3", args, 0, &ret, &err);

            REQUIRE(err.error == GDNATIVE_CALL_OK);
            REQUIRE(ret == "3.1, hi there");
        }
    }
}