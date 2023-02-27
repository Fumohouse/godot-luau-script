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
        SECTION("get_property_type") {
            bool is_valid;
            Variant::Type type = inst->get_property_type("testProperty", &is_valid);

            REQUIRE(is_valid);
            REQUIRE(type == Variant::FLOAT);
        }

        SECTION("get_property_list") {
            uint32_t count;
            GDExtensionPropertyInfo *properties = inst->get_property_list(&count);

            REQUIRE(count == 5);

            REQUIRE(*(StringName *)properties[0].name == StringName("testProperty"));
            REQUIRE(properties[0].type == GDEXTENSION_VARIANT_TYPE_FLOAT);

            REQUIRE(*(StringName *)properties[1].name == StringName("testProperty2"));
            REQUIRE(properties[1].type == GDEXTENSION_VARIANT_TYPE_STRING);

            REQUIRE(*(StringName *)properties[4].name == StringName("custom/testProperty"));
            REQUIRE(properties[4].type == GDEXTENSION_VARIANT_TYPE_FLOAT);

            inst->free_property_list(properties);
        }

        SECTION("property_can_revert") {
            REQUIRE(!inst->property_can_revert("testProperty"));
            REQUIRE(inst->property_can_revert("custom/testProperty"));
        }

        SECTION("property_get_revert") {
            Variant val;
            REQUIRE(inst->property_get_revert("custom/testProperty", &val));
            REQUIRE(val == Variant(1.25));
        }
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

    SECTION("setget") {
        SECTION("set") {
            SECTION("with wrong type") {
                LuauScriptInstance::PropertySetGetError err;
                REQUIRE(!inst->set("testProperty", "asdf", &err));
                REQUIRE(err == LuauScriptInstance::PROP_WRONG_TYPE);
            }

            SECTION("read-only") {
                LuauScriptInstance::PropertySetGetError err;
                REQUIRE(!inst->set("testProperty2", "hey there", &err));
                REQUIRE(err == LuauScriptInstance::PROP_READ_ONLY);
            }
        }

        SECTION("get") {
            SECTION("write-only") {
                LuauScriptInstance::PropertySetGetError err;
                Variant val;
                REQUIRE(!inst->get("testProperty3", val, &err));
                REQUIRE(err == LuauScriptInstance::PROP_WRITE_ONLY);
            }

            SECTION("default value") {
                Variant val;
                REQUIRE(inst->get("testProperty4", val));
                REQUIRE(val == "hey");
            }
        }

        SECTION("with getter and setter") {
            REQUIRE(inst->set("testProperty", 3.5));

            Variant val;
            REQUIRE(inst->get("testProperty", val));
            REQUIRE(val == Variant(7));
        }

        SECTION("with no getter or setter") {
            REQUIRE(inst->set("testProperty4", "asdf"));

            Variant new_val;
            REQUIRE(inst->get("testProperty4", new_val));
            REQUIRE(new_val == "asdf");
        }

        SECTION("custom") {
            REQUIRE(inst->set("custom/testProperty", 2.25));

            Variant new_val;
            REQUIRE(inst->get("custom/testProperty", new_val));
            REQUIRE(new_val == Variant(2.25));
        }

        SECTION("property state") {
            GDExtensionScriptInstancePropertyStateAdd add = [](GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value, void *p_userdata) {
                ((HashMap<StringName, Variant> *)p_userdata)->insert(*((const StringName *)p_name), *((const Variant *)p_value));
            };

            HashMap<StringName, Variant> state;

            inst->get_property_state(add, &state);

            REQUIRE(state.size() == 4);
            REQUIRE(state["testProperty"] == Variant(6.5));
            REQUIRE(state["testProperty2"] == "hello");
            REQUIRE(state["testProperty4"] == "hey");
            REQUIRE(state["custom/testProperty"] == Variant(1.25));
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

    SECTION("IsScript") {
        SECTION("direct") {
            LuaStackOp<GDClassDefinition>::push(T, script->get_definition());
            lua_setglobal(T, "scriptDef");

            ASSERT_EVAL_EQ(T, "return obj:IsA(scriptDef)", bool, true)
        }

        SECTION("base") {
            LuaStackOp<GDClassDefinition>::push(T, base->get_definition());
            lua_setglobal(T, "baseDef");

            ASSERT_EVAL_EQ(T, "return obj:IsA(baseDef)", bool, true)
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
                SECTION("cyclic inheritance") {
                    String new_src_base = script_base->_get_source_code().replace("Node", "require('Script')");
                    script_base->_set_source_code(new_src_base);
                    script->_update_exports();

                    REQUIRE(script_base->_is_placeholder_fallback_enabled());
                }

                SECTION("base script updating") {
                    REQUIRE(script->has_dependency(script_base));

                    String new_src = script->_get_source_code().replace("Base", "Base2");
                    script->_set_source_code(new_src);
                    script->_update_exports();

                    REQUIRE(!script->has_dependency(script_base));
                    REQUIRE(script->has_dependency(script_base2));

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
            String new_src = module->_get_source_code().replace("--@1", "require('Module2.mod')");
            module->_set_source_code(new_src);
            LuauLanguage::get_singleton()->_reload_tool_script(module, false);

            REQUIRE(!script_base->_is_valid());
        }

        SECTION("class-class") {
            String new_src = script_base->_get_source_code().replace("--@1", "require('Script')");
            script_base->_set_source_code(new_src);
            LuauLanguage::get_singleton()->_reload_tool_script(script_base, false);

            REQUIRE(!script_base->_is_valid());
            REQUIRE(!script->_is_valid());
        }

        SECTION("module-class") {
            String new_src = module->_get_source_code().replace("--@1", "require('Base')");
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
