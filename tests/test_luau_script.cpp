#include <catch_amalgamated.hpp>

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "gd_luau.h"
#include "luagd_stack.h"
#include "luau_cache.h"
#include "luau_lib.h"
#include "luau_script.h"

#include "test_utils.h"

TEST_CASE("luau script: script load") {
    // singleton is not available during test runs.
    // this will construct the singleton then destroy it at the end of the scope.
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script, "script_load/Script.lua")

    SECTION("method methods") {
        REQUIRE(script->is_tool());
        REQUIRE(script->get_script_method_list().size() == 2);
        REQUIRE(script->_has_method("TestMethod"));
        REQUIRE(script->_get_method_info("TestMethod") == GDMethod({ "TestMethod" }).operator Dictionary());

        SECTION("method name conversion") {
            StringName actual_name;

            SECTION("normal") {
                REQUIRE(script->has_method("test_method", &actual_name));
                REQUIRE(actual_name == StringName("TestMethod"));
            }

            SECTION("leading underscores") {
                REQUIRE(script->has_method("__weird_method_name", &actual_name));
                REQUIRE(actual_name == StringName("__WeirdMethodName"));
            }
        }
    }

    SECTION("property methods") {
        REQUIRE(script->get_script_property_list().size() == 1);
        REQUIRE(script->_get_members()[0] == StringName("testProperty"));
        REQUIRE(script->_has_property_default_value("testProperty"));
        REQUIRE(script->_get_property_default_value("testProperty") == Variant(5.5));
    }

    SECTION("signal methods") {
        REQUIRE(script->_has_script_signal("testSignal"));
        REQUIRE(script->_get_script_signal_list()[0].get("args").operator Array().size() == 1);
    }

    SECTION("misc") {
        REQUIRE(script->get_instance_base_type() == StringName("RefCounted"));
        REQUIRE(script->_get_rpc_config().operator Dictionary().has("TestRpc"));
    }
}

TEST_CASE("luau script: instance") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script, "instance/Script.lua")

    Object obj;
    obj.set_script(script);

    LuauScriptInstance *inst = script->instance_get(&obj);

    REQUIRE(inst->get_owner()->get_instance_id() == obj.get_instance_id());
    REQUIRE(inst->get_script() == script);

    // Global table access
    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    LuaStackOp<Object *>::push(T, &obj);
    lua_setglobal(T, "obj");

    SECTION("method methods") {
        REQUIRE(inst->has_method("TestMethod"));
        REQUIRE(inst->has_method("TestMethod2"));

        uint32_t count;
        GDExtensionMethodInfo *methods = inst->get_method_list(&count);

        REQUIRE(count == 5);

        bool m1_found = false;
        bool m2_found = false;

        for (int i = 0; i < count; i++) {
            StringName *name = (StringName *)methods[i].name;

            if (*name == StringName("TestMethod")) {
                m1_found = true;
            } else if (*name == StringName("TestMethod2")) {
                m2_found = true;

                REQUIRE(methods[i].return_value.type == GDEXTENSION_VARIANT_TYPE_FLOAT);
                REQUIRE(methods[i].argument_count == 2);
                REQUIRE(*((StringName *)methods[i].arguments[1].name) == StringName("arg2"));
            }
        }

        REQUIRE(m1_found);
        REQUIRE(m2_found);

        inst->free_method_list(methods);
    }

    SECTION("property methods") {
        bool is_valid;
        Variant::Type type = inst->get_property_type("testProperty", &is_valid);

        REQUIRE(is_valid);
        REQUIRE(type == Variant::FLOAT);

        uint32_t count;
        GDExtensionPropertyInfo *properties = inst->get_property_list(&count);

        REQUIRE(count == 4);

        bool p1_found = false;
        bool p2_found = false;

        for (int i = 0; i < count; i++) {
            StringName *name = (StringName *)properties[i].name;

            if (*name == StringName("testProperty")) {
                p1_found = true;
                REQUIRE(properties[i].type == GDEXTENSION_VARIANT_TYPE_FLOAT);
            } else if (*name == StringName("testProperty2")) {
                p2_found = true;
                REQUIRE(properties[i].type == GDEXTENSION_VARIANT_TYPE_STRING);
            }
        }

        REQUIRE(p1_found);
        REQUIRE(p2_found);

        inst->free_property_list(properties);
    }

    SECTION("call") {
        SECTION("normal operation") {
            const Variant args[] = { 2.5f, "Hello world" };
            const Variant *pargs[] = { &args[0], &args[1] };

            Variant ret;
            GDExtensionCallError err;

            inst->call("TestMethod", pargs, 2, &ret, &err);

            REQUIRE(err.error == GDEXTENSION_CALL_OK);
            REQUIRE(ret == "2.5, Hello world");
        }

        SECTION("virtual method name conversion") {
            Variant ret;
            GDExtensionCallError err;

            inst->call("_ready", nullptr, 0, &ret, &err);

            REQUIRE(err.error == GDEXTENSION_CALL_OK);
        }

        SECTION("default argument") {
            const Variant args[] = { 5.3f };
            const Variant *pargs[] = { &args[0] };

            Variant ret;
            GDExtensionCallError err;

            inst->call("TestMethod", pargs, 1, &ret, &err);

            REQUIRE(err.error == GDEXTENSION_CALL_OK);
            REQUIRE(ret == "5.3, hi");
        }

        SECTION("invalid arguments") {
            SECTION("too few") {
                Variant ret;
                GDExtensionCallError err;

                inst->call("TestMethod", nullptr, 0, &ret, &err);

                REQUIRE(err.error == GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS);
                REQUIRE(err.argument == 1);
            }

            SECTION("too many") {
                const Variant one = 1;
                const Variant *pargs[] = { &one, &one, &one, &one, &one };
                Variant ret;
                GDExtensionCallError err;

                inst->call("TestMethod", pargs, 5, &ret, &err);

                REQUIRE(err.error == GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS);
                REQUIRE(err.argument == 2);
            }

            SECTION("invalid") {
                const Variant args[] = { false };
                const Variant *pargs[] = { &args[0] };

                Variant ret;
                GDExtensionCallError err;

                inst->call("TestMethod", pargs, 1, &ret, &err);

                REQUIRE(err.error == GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT);
                REQUIRE(err.argument == 0);
                REQUIRE(err.expected == GDEXTENSION_VARIANT_TYPE_FLOAT);
            }
        }
    }

    SECTION("table setget") {
        SECTION("normal") {
            lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
            int top = lua_gettop(L);

            SECTION("set") {
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

            SECTION("get") {
                lua_pushstring(L, "PrivateMethod");
                bool is_valid = inst->table_get(L);

                REQUIRE(is_valid);
                REQUIRE(lua_isLfunction(L, -1));
                REQUIRE(lua_gettop(L) == top + 1);
            }
        }

        SECTION("wrong thread") {
            lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_SCRIPT_LOAD);

            SECTION("set") {
                lua_pushstring(L, "testField");
                lua_pushstring(L, "asdf");

                bool is_valid = inst->table_set(L);
                REQUIRE(!is_valid);
            }

            SECTION("get") {
                lua_pushstring(L, "PrivateMethod");

                bool is_valid = inst->table_get(L);
                REQUIRE(!is_valid);
            }
        }
    }

    SECTION("notification") {
        inst->notification(42);

        LuaStackOp<String>::push(T, "_notifHits");
        inst->table_get(T);

        REQUIRE(LuaStackOp<int>::check(T, -1) == 1);
    }

    SECTION("to string") {
        bool is_valid;
        String out;
        inst->to_string((GDExtensionBool *)&is_valid, &out);

        REQUIRE(is_valid);
        REQUIRE(out == "my awesome class");
    }

    SECTION("setget") {
        SECTION("set") {
            SECTION("with wrong type") {
                LuauScriptInstance::PropertySetGetError err;
                bool is_valid = inst->set("testProperty", "asdf", &err);

                REQUIRE(!is_valid);
                REQUIRE(err == LuauScriptInstance::PROP_WRONG_TYPE);
            }

            SECTION("read-only") {
                LuauScriptInstance::PropertySetGetError err;
                bool is_valid = inst->set("testProperty2", "hey there", &err);

                REQUIRE(!is_valid);
                REQUIRE(err == LuauScriptInstance::PROP_READ_ONLY);
            }
        }

        SECTION("get") {
            SECTION("write-only") {
                LuauScriptInstance::PropertySetGetError err;
                Variant val;
                bool is_valid = inst->get("testProperty3", val, &err);

                REQUIRE(!is_valid);
                REQUIRE(err == LuauScriptInstance::PROP_WRITE_ONLY);
            }

            SECTION("default value") {
                Variant val;
                bool get_is_valid = inst->get("testProperty4", val);

                REQUIRE(get_is_valid);
                REQUIRE(val == "hey");
            }
        }

        SECTION("with getter and setter") {
            bool set_is_valid = inst->set("testProperty", 3.5);
            REQUIRE(set_is_valid);

            Variant val;
            bool get_is_valid = inst->get("testProperty", val);

            REQUIRE(get_is_valid);
            REQUIRE(val == Variant(7));
        }

        SECTION("with no getter or setter") {
            bool set_is_valid = inst->set("testProperty4", "asdf");
            REQUIRE(set_is_valid);

            Variant new_val;
            bool get_is_valid = inst->get("testProperty4", new_val);

            REQUIRE(get_is_valid);
            REQUIRE(new_val == "asdf");
        }

        SECTION("property state") {
            GDExtensionScriptInstancePropertyStateAdd add = [](GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value, void *p_userdata) {
                ((HashMap<StringName, Variant> *)p_userdata)->insert(*((const StringName *)p_name), *((const Variant *)p_value));
            };

            HashMap<StringName, Variant> state;

            inst->get_property_state(add, &state);

            REQUIRE(state.size() == 3);
            REQUIRE(state["testProperty"] == Variant(6.5));
            REQUIRE(state["testProperty2"] == "hello");
            REQUIRE(state["testProperty4"] == "hey");
        }
    }

    SECTION("metatable") {
        SECTION("namecall") {
            SECTION("from table index") {
                ASSERT_EVAL_EQ(T, "return obj:PrivateMethod()", String, "hi there");
            }

            SECTION("non-registered method") {
                ASSERT_EVAL_EQ(T, "return obj:NonRegisteredMethod()", String, "what's up");
            }

            SECTION("default args") {
                ASSERT_EVAL_EQ(T, "return obj:TestMethod(2.5)", String, "2.5, hi");
            }

            SECTION("registered method") {
                ASSERT_EVAL_EQ(T, "return obj:TestMethod(2.5, 'asdf')", String, "2.5, asdf");
            }
        }

        SECTION("newindex") {
            SECTION("signal read-only") {
                ASSERT_EVAL_FAIL(T, "obj.testSignal = 1234", "exec:1: cannot assign to signal 'testSignal'");
            }

            SECTION("constant read-only") {
                ASSERT_EVAL_FAIL(T, "obj.TEST_CONSTANT = 1234", "exec:1: cannot assign to constant 'TEST_CONSTANT'");
            }

            SECTION("registered") {
                EVAL_THEN(T, "obj.testProperty = 2.5", {
                    Variant ret;
                    bool is_valid = inst->get("testProperty", ret);

                    REQUIRE(is_valid);
                    REQUIRE(ret == Variant(5));
                });
            }

            SECTION("non registered") {
                EVAL_THEN(T, "obj.testField = 2", {
                    LuaStackOp<String>::push(T, "testField");
                    bool is_valid = inst->table_get(T);

                    REQUIRE(is_valid);
                    REQUIRE(LuaStackOp<int>::get(T, -1) == 2);
                });
            }

            SECTION("read-only") {
                ASSERT_EVAL_FAIL(T, "obj.testProperty2 = 'asdf'", "exec:1: property 'testProperty2' is read-only");
            }
        }

        SECTION("index") {
            SECTION("signal") {
                ASSERT_EVAL_EQ(T, "return obj.testSignal", Signal, Signal(&obj, "testSignal"));
            }

            SECTION("constant") {
                ASSERT_EVAL_EQ(T, "return obj.TEST_CONSTANT", Vector2, Vector2(1, 2));
            }

            SECTION("registered") {
                ASSERT_EVAL_EQ(T, "return obj.testProperty", float, 6.5f);
            }

            SECTION("non registered") {
                ASSERT_EVAL_EQ(T, "return obj.testField", int, 1);
            }

            SECTION("write-only") {
                ASSERT_EVAL_FAIL(T, "return obj.testProperty3", "exec:1: property 'testProperty3' is write-only")
            }
        }

        lua_pop(L, 1); // thread
    }

    SECTION("Callable") {
        SECTION("instance method"){
            ASSERT_EVAL_EQ(T, R"ASDF(
                return Callable.new(obj, "TestMethod")
            )ASDF",
                    Callable, Callable(&obj, "TestMethod"))
        }

        SECTION("no permissions") {
            ASSERT_EVAL_FAIL(T, R"ASDF(
                return Callable.new(obj, "Call")
            )ASDF",
                    "exec:2: !!! THREAD PERMISSION VIOLATION: attempted to access 'Godot.Object.Object.Call'. needed permissions: 1, got: 0 !!!")
        }
    }

    SECTION("instantiation") {
        LuaStackOp<GDClassDefinition>::push(T, script->get_definition());
        lua_setglobal(T, "classDef");

        EVAL_THEN(T, "return classDef.new()", {
            REQUIRE(LuaStackOp<Object *>::is(T, -1));

            Object *obj = LuaStackOp<Object *>::get(T, -1);
            REQUIRE(obj->get_script() == script);
            REQUIRE(obj->get_class() == "RefCounted");
        });
    }
}

TEST_CASE("luau script: inheritance") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(base, "inheritance/Base.lua")
    LOAD_SCRIPT_FILE(script, "inheritance/Script.lua")

    // Instance
    Object obj;
    obj.set_script(script);

    LuauScriptInstance *inst = script->instance_get(&obj);

    lua_State *L = GDLuau::get_singleton()->get_vm(GDLuau::VM_CORE);
    lua_State *T = lua_newthread(L);
    luaL_sandboxthread(T);

    LuaStackOp<Object *>::push(T, &obj);
    lua_setglobal(T, "obj");

    SECTION("methods") {
        SECTION("inherited") {
            ASSERT_EVAL_EQ(T, "return obj.property1", String, "hey");
        }

        SECTION("overriden") {
            ASSERT_EVAL_EQ(T, "return obj.property2", String, "hihi");
        }
    }

    SECTION("properties") {
        SECTION("inherited") {
            ASSERT_EVAL_EQ(T, "return obj:Method1()", String, "there");
        }

        SECTION("overridden") {
            ASSERT_EVAL_EQ(T, "return obj:Method2()", String, "guy");
        }
    }

    lua_pop(L, 1); // thread
}

TEST_CASE("luau script: base script loading") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script_base, "base_script/Base.lua")
    LOAD_SCRIPT_FILE(script, "base_script/Script.lua")

    SECTION("base loading") {
        REQUIRE(script->get_base() == script_base);
    }

    SECTION("base invalid") {
        String orig_src = script_base->_get_source_code();
        String new_src = orig_src.replace("--@1", "@#%^!@*&#syntaxerror");
        script_base->_set_source_code(new_src);

        script_base->_reload(true);
        script->_reload(true);

        REQUIRE(!script_base->_is_valid());
        REQUIRE(!script->_is_valid());

        SECTION("base restored") {
            script_base->_set_source_code(orig_src);

            script_base->_reload(true);
            script->_reload(true);

            REQUIRE(script_base->_is_valid());
            REQUIRE(script->_is_valid());
        }
    }
}

TEST_CASE("luau script: placeholders") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script_base, "placeholder/Base.lua")
    LOAD_SCRIPT_FILE(script_base2, "placeholder/Base2.lua")
    LOAD_SCRIPT_FILE(script, "placeholder/Script.lua")

    Object obj;

    SECTION("placeholder") {
        PlaceHolderScriptInstance *placeholder = memnew(PlaceHolderScriptInstance(script, &obj));

        SECTION("setget") {
            Variant val;

            REQUIRE(placeholder->get("testProperty", val));
            REQUIRE(val == Variant(4.25));

            REQUIRE(placeholder->get("baseProperty", val));
            REQUIRE(val == Variant("hello"));
        }

        SECTION("update_exports") {
            SECTION("script invalid") {
                // Set beforehand - default values are not available when the script is invalid
                REQUIRE(placeholder->set("testProperty", Variant(4.75)));

                String new_src = script->_get_source_code().replace("--@1", "[!@#}^syntaxerror");
                script->_set_source_code(new_src);
                script->_update_exports();

                REQUIRE(script->_is_placeholder_fallback_enabled());

                Variant val;
                REQUIRE(placeholder->property_get_fallback("testProperty", val));
                REQUIRE(val == Variant(4.75));
            }

            SECTION("script change") {
                String new_src = script->_get_source_code().replace("--@1", R"ASDF(
                    Script:RegisterProperty("testProperty2", Enum.VariantType.VECTOR3)
                        :Default(Vector3.new(1, 2, 3))
                )ASDF");
                script->_set_source_code(new_src);
                script->_update_exports();

                Variant val;
                REQUIRE(placeholder->get("testProperty", val));
                REQUIRE(val == Variant(4.25));

                REQUIRE(placeholder->get("testProperty2", val));
                REQUIRE(val == Variant(Vector3(1, 2, 3)));
            }

            SECTION("base script") {
                String path = script->get_path();

                SECTION("cyclic inheritance") {
                    String new_src_base = script_base->_get_source_code().replace("\"Node\"", "\"Script.lua\"");
                    script_base->_set_source_code(new_src_base);
                    script->_update_exports();

                    REQUIRE(!script->_is_valid());
                    REQUIRE(!script_base->_is_valid());
                }

                SECTION("base script updating") {
                    REQUIRE(script_base->has_dependent(path));

                    String new_src = script->_get_source_code().replace("\"Base.lua\"", "\"Base2.lua\"");
                    script->_set_source_code(new_src);
                    script->_update_exports();

                    REQUIRE(!script_base->has_dependent(path));
                    REQUIRE(script_base2->has_dependent(path));

                    Variant val;

                    REQUIRE(!placeholder->get("baseProperty", val));

                    REQUIRE(placeholder->get("baseProperty2", val));
                    REQUIRE(val == Variant(Vector2(1, 2)));
                }
            }
        }

        memdelete(placeholder);
    }
}

TEST_CASE("luau script: require") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script_base, "require/Base.lua")
    LOAD_SCRIPT_FILE(script, "require/Script.lua")
    LOAD_SCRIPT_MOD_FILE(module, "require/Module.mod.lua")
    LOAD_SCRIPT_MOD_FILE(module2, "require/Module2.mod.lua")

    SECTION("requiring a class") {
        REQUIRE(script->get_property("baseMsg").default_value == Variant("what's up"));
    }

    SECTION("requiring a dedicated module") {
        REQUIRE(script_base->get_property("baseProperty").default_value == Variant("hello"));
    }

    SECTION("cyclic dependencies") {
        SECTION("module-module") {
            String new_src = module->_get_source_code().replace("--@1", "require('test_scripts/require/Module2.mod')");
            module->_set_source_code(new_src);
            LuauLanguage::get_singleton()->_reload_tool_script(module, false);

            REQUIRE(!script_base->_is_valid());
        }

        SECTION("class-class") {
            String new_src = script_base->_get_source_code().replace("--@1", "require('test_scripts/require/Script')");
            script_base->_set_source_code(new_src);
            LuauLanguage::get_singleton()->_reload_tool_script(script_base, false);

            REQUIRE(!script_base->_is_valid());
            REQUIRE(!script->_is_valid());
        }

        SECTION("module-class") {
            String new_src = module->_get_source_code().replace("--@1", "require('test_scripts/require/Base')");
            module->_set_source_code(new_src);
            LuauLanguage::get_singleton()->_reload_tool_script(module, false);

            REQUIRE(!script_base->_is_valid());
        }
    }
}

TEST_CASE("luau script: reloading at runtime") {
    GDLuau gd_luau;
    LuauCache luau_cache;

    LOAD_SCRIPT_FILE(script_base, "reload/Base.lua")
    LOAD_SCRIPT_FILE(script, "reload/Script.lua")
    LOAD_SCRIPT_MOD_FILE(module, "reload/Module.mod.lua")

    Object base_obj;
    base_obj.set_script(script_base);
    LuauScriptInstance *inst_base = script_base->instance_get(&base_obj);
    inst_base->set("baseProperty", "hey");

    Object obj;
    obj.set_script(script);
    LuauScriptInstance *inst = script->instance_get(&obj);
    inst->set("testProperty", 5.25);

    Variant val;

    SECTION("reload") {
        String new_src = script->_get_source_code().replace("--@1", R"ASDF(
            Script:RegisterProperty("testProperty2", Enum.VariantType.FLOAT)
                :Default(1.25)
        )ASDF");
        script->_set_source_code(new_src);
        LuauLanguage::get_singleton()->_reload_tool_script(script, false);

        inst = script->instance_get(&obj);
        REQUIRE(inst->get("testProperty2", val));
        REQUIRE(val == Variant(1.25));
    }

    SECTION("reload base reloads inherited") {
        String new_src = script_base->_get_source_code().replace("--@1", R"ASDF(
            Base:RegisterProperty("baseProperty2", Enum.VariantType.FLOAT)
                :Default(1.5)
        )ASDF");
        script_base->_set_source_code(new_src);
        LuauLanguage::get_singleton()->_reload_tool_script(script_base, false);

        inst_base = script_base->instance_get(&base_obj);
        REQUIRE(inst_base->get("baseProperty2", val));
        REQUIRE(val == Variant(1.5));

        inst = script->instance_get(&obj);
    }

    SECTION("invalid then valid keeps state") {
        String old_src = script->_get_source_code();
        String new_src = old_src.replace("--@1", "#^%}^)[}(_+[}-(");
        script->_set_source_code(new_src);
        LuauLanguage::get_singleton()->_reload_tool_script(script, false);

        REQUIRE(!script->_is_valid());
        REQUIRE(!script->pending_reload_state.is_empty());

        script->_set_source_code(old_src);
        LuauLanguage::get_singleton()->_reload_tool_script(script, false);

        REQUIRE(script->_is_valid());
        REQUIRE(script->pending_reload_state.is_empty());

        inst = script->instance_get(&obj);
    }

    SECTION("reload module reloads dependencies") {
        REQUIRE(script_base->get_property("baseProperty").default_value == Variant("hello"));

        String new_src = module->_get_source_code().replace("hello", "hey there");
        module->_set_source_code(new_src);
        LuauLanguage::get_singleton()->_reload_tool_script(module, false);

        REQUIRE(script_base->get_property("baseProperty").default_value == Variant("hey there"));

        inst_base = script_base->instance_get(&base_obj);
        inst = script->instance_get(&obj);
    }

    REQUIRE(inst->get("testProperty", val));
    REQUIRE(val == Variant(5.25));

    REQUIRE(inst_base->get("baseProperty", val));
    REQUIRE(val == Variant("hey"));
}
