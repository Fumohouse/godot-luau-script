#include "luau_lib.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <string.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_bindings.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
#include "luagd_variant.h"
#include "luau_script.h"

using namespace godot;

#define CLASS_MT_NAME "Luau.GDClassDefinition"
#define CLASS_PROPERTY_MT_NAME "Luau.GDClassProperty"
#define METHOD_MT_NAME "Luau.GDMethod"

UDATA_STACK_OP_IMPL(GDClassDefinition, CLASS_MT_NAME, DTOR(GDClassDefinition))

PTR_OP_DEF(GDClassProperty)
PTR_STACK_OP_IMPL(GDClassProperty, CLASS_PROPERTY_MT_NAME)

PTR_OP_DEF(GDMethod)
PTR_STACK_OP_IMPL(GDMethod, METHOD_MT_NAME)

/* STRUCTS */

GDProperty::operator Dictionary() const {
    Dictionary dict;

    dict["type"] = type;
    dict["usage"] = usage;

    dict["name"] = name;
    dict["class_name"] = class_name;

    dict["hint"] = hint;
    dict["hint_string"] = hint_string;

    return dict;
}

GDProperty::operator Variant() const {
    return this->operator Dictionary();
}

GDMethod::operator Dictionary() const {
    Dictionary dict;

    dict["name"] = name;
    dict["return"] = return_val;
    dict["flags"] = flags;

    Array args;
    for (const GDProperty &arg : arguments)
        args.push_back(arg);

    dict["args"] = args;

    Array default_args;
    for (const Variant &default_arg : default_arguments)
        default_args.push_back(default_arg);

    dict["default_args"] = default_args;

    return dict;
}

GDMethod::operator Variant() const {
    return operator Dictionary();
}

GDRpc::operator Dictionary() const {
    Dictionary dict;

    dict["rpc_mode"] = rpc_mode;
    dict["transfer_mode"] = transfer_mode;
    dict["call_local"] = call_local;
    dict["channel"] = channel;

    return dict;
}

GDRpc::operator Variant() const {
    return operator Dictionary();
}

int GDClassDefinition::set_prop(const String &name, const GDClassProperty &prop) {
    HashMap<StringName, uint64_t>::ConstIterator E = property_indices.find(name);

    if (E) {
        properties.set(E->value, prop);
        return E->value;
    } else {
        int index = properties.size();
        property_indices[name] = index;
        properties.push_back(prop);

        return index;
    }
}

/* PROPERTY */

GDProperty luascript_read_property(lua_State *L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);

    GDProperty property;

    if (luaGD_getfield(L, idx, "type"))
        property.type = static_cast<GDExtensionVariantType>(luaGD_checkvaluetype<uint32_t>(L, -1, "type", LUA_TNUMBER));

    if (luaGD_getfield(L, idx, "name"))
        property.name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "hint"))
        property.hint = static_cast<PropertyHint>(luaGD_checkvaluetype<uint32_t>(L, -1, "hint", LUA_TNUMBER));

    if (luaGD_getfield(L, idx, "hintString"))
        property.hint_string = luaGD_checkvaluetype<String>(L, -1, "hintString", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "usage"))
        property.usage = luaGD_checkvaluetype<uint32_t>(L, -1, "usage", LUA_TNUMBER);

    if (luaGD_getfield(L, idx, "className"))
        property.class_name = luaGD_checkvaluetype<String>(L, -1, "className", LUA_TSTRING);

    return property;
}

static GDRpc luascript_read_rpc(lua_State *L, int idx) {
    GDRpc rpc;

    if (luaGD_getfield(L, idx, "rpcMode")) {
        if (!lua_isnumber(L, -1))
            luaGD_valueerror(L, "rpcMode", luaL_typename(L, -1), "MultiplayerAPI.RPCMode");

        rpc.rpc_mode = (MultiplayerAPI::RPCMode)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    if (luaGD_getfield(L, idx, "transferMode")) {
        if (!lua_isnumber(L, -1))
            luaGD_valueerror(L, "rpcMode", luaL_typename(L, -1), "MultiplayerPeer.TransferMode");

        rpc.transfer_mode = (MultiplayerPeer::TransferMode)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    if (luaGD_getfield(L, idx, "callLocal"))
        rpc.call_local = luaGD_checkvaluetype<bool>(L, -1, "callLocal", LUA_TBOOLEAN);

    if (luaGD_getfield(L, idx, "channel"))
        rpc.channel = luaGD_checkvaluetype<int>(L, -1, "channel", LUA_TNUMBER);

    return rpc;
}

/* CLASS */

static int luascript_gdclass(lua_State *L) {
    GDClassDefinition def;
    def.name = luaL_optstring(L, 1, "");
    def.extends = luaL_optstring(L, 2, "RefCounted");

    LuaStackOp<GDClassDefinition>::push(L, def);
    return 1;
}

#define luascript_gdclass_readonly_error(L) luaL_error(L, "this definition is read-only")

static int luascript_gdclass_namecall(lua_State *L) {
    if (const char *key = lua_namecallatom(L, nullptr)) {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
        if (def->is_readonly)
            luascript_gdclass_readonly_error(L);

        // Chaining functions
        if (strcmp(key, "Tool") == 0) {
            def->is_tool = LuaStackOp<bool>::check(L, 2);

            lua_settop(L, 1); // return def
            return 1;
        }

        if (strcmp(key, "Permissions") == 0) {
            int32_t permissions = LuaStackOp<int32_t>::check(L, 2);

            GDThreadData *udata = luaGD_getthreaddata(L);

            String path = udata->script->get_path();
            if (path.is_empty() || LuauLanguage::get_singleton() == nullptr || !LuauLanguage::get_singleton()->is_core_script(path))
                luaL_error(L, "!!! cannot set permissions on a non-core script !!!");

            def->permissions = static_cast<ThreadPermissions>(permissions);

            lua_settop(L, 1); // return def
            return 1;
        }

        if (strcmp(key, "IconPath") == 0) {
            def->icon_path = LuaStackOp<String>::check(L, 2);

            lua_settop(L, 1); // return def;
            return 1;
        }

        if (strcmp(key, "RegisterImpl") == 0) {
            if (def->table_ref != -1)
                lua_unref(L, def->table_ref);

            luaL_checktype(L, 2, LUA_TTABLE);
            def->table_ref = lua_ref(L, 2);

            lua_settop(L, 1); // return def;
            return 1;
        }

        // "Raw" functions
        if (strcmp(key, "RegisterMethod") == 0) {
            String name = LuaStackOp<String>::check(L, 2);

            GDMethod method;
            method.name = name;
            def->methods[name] = method;

            LuaStackOp<GDMethod *>::push(L, &def->methods[name]);
            return 1;
        }

        if (strcmp(key, "RegisterProperty") == 0) {
            String name = LuaStackOp<String>::check(L, 2);

            GDClassProperty class_prop;

            int type = lua_type(L, 3);
            if (type == LUA_TTABLE) {
                class_prop.property = luascript_read_property(L, 3);
            } else if (type == LUA_TNUMBER) {
                class_prop.property.type = static_cast<GDExtensionVariantType>(lua_tointeger(L, 3));
            }

            class_prop.property.name = name;

            int idx = def->set_prop(name, class_prop);

            LuaStackOp<GDClassProperty *>::push(L, &def->properties.ptrw()[idx]);
            return 1;
        }

        if (strcmp(key, "RegisterSignal") == 0) {
            String name = LuaStackOp<String>::check(L, 2);

            // Signals are stored as methods with only name and arguments
            GDMethod signal;
            signal.name = name;
            signal.is_signal = true;

            def->signals[name] = signal;

            LuaStackOp<GDMethod *>::push(L, &def->signals[name]);
            return 1;
        }

        if (strcmp(key, "RegisterRpc") == 0) {
            String name = LuaStackOp<String>::check(L, 2);
            luaL_checktype(L, 3, LUA_TTABLE);

            GDRpc rpc = luascript_read_rpc(L, 3);
            rpc.name = name;

            def->rpcs[name] = rpc;

            return 0;
        }

        if (strcmp(key, "RegisterConstant") == 0) {
            String name = LuaStackOp<String>::check(L, 2);
            Variant value = LuaStackOp<Variant>::check(L, 3);

            def->constants[name] = value;

            return 0;
        }

        // Helpers
#define PROPERTY_HELPER(func_name, prop_usage)         \
    if (strcmp(key, func_name) == 0) {                 \
        String name = LuaStackOp<String>::check(L, 2); \
                                                       \
        GDClassProperty prop;                          \
        prop.property.name = name;                     \
        prop.property.usage = prop_usage;              \
                                                       \
        def->set_prop(name, prop);                     \
                                                       \
        return 0;                                      \
    }

        PROPERTY_HELPER("PropertyGroup", PROPERTY_USAGE_GROUP)
        PROPERTY_HELPER("PropertySubgroup", PROPERTY_USAGE_SUBGROUP)
        PROPERTY_HELPER("PropertyCategory", PROPERTY_USAGE_CATEGORY)

        luaGD_nomethoderror(L, key, "GDClassDefinition");
    }

    luaGD_nonamecallatomerror(L);
}

static int luascript_gdclass_newindex(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    if (def->is_readonly)
        luascript_gdclass_readonly_error(L);

    const char *key = luaL_checkstring(L, 2);

    if (strcmp(key, "new") == 0)
        luaL_error(L, "cannot set constructor 'new' on GDClassDefinition");

    if (def->table_ref == -1) {
        // Create def table in registry.
        lua_newtable(L);
        def->table_ref = lua_ref(L, -1);
    } else {
        lua_getref(L, def->table_ref);
    }

    lua_insert(L, -3);
    lua_settable(L, -3);

    return 0;
}

static int luascript_gdclass_new(lua_State *L) {
    GDClassDefinition *def = luaGD_lightudataup<GDClassDefinition>(L, 1);

    if (def->script == nullptr)
        luaL_error(L, "cannot instantiate: script is unknown");

    if (def->script->is_reloading())
        luaL_error(L, "cannot instantiate: script is loading");

    StringName class_name = def->script->_get_instance_base_type();

    GDExtensionObjectPtr ptr = internal::gde_interface->classdb_construct_object(&class_name);
    GDObjectInstanceID id = internal::gde_interface->object_get_instance_id(ptr);
    Object *obj = ObjectDB::get_instance(id);
    obj->set_script(Ref<LuauScript>(def->script));

    LuaStackOp<Object *>::push(L, obj);
    return 1;
}

static int luascript_gdclass_index(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    const char *key = luaL_checkstring(L, 2);

    if (strcmp(key, "new") == 0) {
        lua_pushlightuserdata(L, def);
        lua_pushcclosure(L, luascript_gdclass_new, "GDClassDefinition.new", 1);
        return 1;
    }

    if (def->table_ref == -1) {
        luaL_error(L, "failed to get method on class definition: table ref is invalid");
    }

    lua_getref(L, def->table_ref);
    lua_getfield(L, -1, key);

    return 1;
}

/* CHAINING METAMETHODS */

static int luascript_method_namecall(lua_State *L) {
    GDMethod *method = LuaStackOp<GDMethod *>::check(L, 1);

    if (const char *key = lua_namecallatom(L, nullptr)) {
        if (strcmp(key, "Args") == 0) {
            int top = lua_gettop(L);
            method->arguments.resize(top - 1);

            for (int i = 2; i <= top; i++) {
                method->arguments.set(i - 2, luascript_read_property(L, i));
            }
        } else if (method->is_signal) {
            // Signal does not get any of the rest of the methods
            luaGD_nomethoderror(L, key, "GDMethod (Signal)");
        } else if (strcmp(key, "DefaultArgs") == 0) {
            int top = lua_gettop(L);
            method->default_arguments.resize(top - 1);

            for (int i = 2; i <= top; i++) {
                method->default_arguments.set(i - 2, LuaStackOp<Variant>::check(L, i));
            }
        } else if (strcmp(key, "ReturnVal") == 0) {
            method->return_val = luascript_read_property(L, 2);
        } else if (strcmp(key, "Flags") == 0) {
            method->flags = LuaStackOp<uint32_t>::check(L, 2);
        } else {
            luaGD_nomethoderror(L, key, "GDMethod");
        }

        lua_settop(L, 1); // return method
        return 1;
    }

    luaGD_nonamecallatomerror(L);
}

#define luaGD_hinterror(L, kind, non) luaL_error(L, "cannot set %s hint on non-%s property", kind, non)

static String luascript_stringhintlist(lua_State *L, int start_index) {
    String hint_string = LuaStackOp<String>::check(L, start_index);

    int top = lua_gettop(L);
    for (int i = start_index + 1; i <= top; i++)
        hint_string = hint_string + "," + LuaStackOp<String>::check(L, i);

    return hint_string;
}

static int luascript_classprop_namecall(lua_State *L) {
    GDClassProperty *prop = LuaStackOp<GDClassProperty *>::check(L, 1);

    if (const char *key = lua_namecallatom(L, nullptr)) {
        if (strcmp(key, "Default") == 0) {
            // NIL type means value must be nil. Godot does not support setting the value of a NIL property.
            if (prop->property.type == GDEXTENSION_VARIANT_TYPE_NIL) {
                luaL_checktype(L, 2, LUA_TNIL);
                prop->default_value = Variant();
            } else {
                if (!LuauVariant::lua_is(L, 2, prop->property.type, prop->property.class_name)) {
                    String type_name;
                    if (prop->property.class_name.is_empty()) {
                        type_name = Variant::get_type_name((Variant::Type)prop->property.type);
                    } else {
                        type_name = prop->property.class_name;
                    }

                    luaL_typeerror(L, 2, type_name.utf8().get_data());
                }

                prop->default_value = LuaStackOp<Variant>::check(L, 2);
            }
        } else if (strcmp(key, "SetGet") == 0) {
            prop->setter = luaL_optstring(L, 2, "");
            prop->getter = luaL_optstring(L, 3, "");

            // HELPERS
        } else if (strcmp(key, "Range") == 0) {
            Array hint_values;
            hint_values.resize(3);

            if (prop->property.type == GDEXTENSION_VARIANT_TYPE_INT) {
                int min = luaL_checkinteger(L, 2);
                int max = luaL_checkinteger(L, 3);
                int step = luaL_optinteger(L, 4, 1);

                hint_values[0] = min;
                hint_values[1] = max;
                hint_values[2] = step;
            } else if (prop->property.type == GDEXTENSION_VARIANT_TYPE_FLOAT) {
                double min = luaL_checknumber(L, 2);
                double max = luaL_checknumber(L, 3);
                double step = luaL_optnumber(L, 4, 1.0);

                hint_values[0] = min;
                hint_values[1] = max;
                hint_values[2] = step;
            } else {
                luaGD_hinterror(L, "range", "numeric");
            }

            prop->property.hint = PROPERTY_HINT_RANGE;
            prop->property.hint_string = String("{0},{1},{2}").format(hint_values);
        } else if (strcmp(key, "Enum") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING &&
                    prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "enum", "string/integer");

            String hint_string = luascript_stringhintlist(L, 2);

            prop->property.hint = PROPERTY_HINT_ENUM;
            prop->property.hint_string = hint_string;
        } else if (strcmp(key, "Suggestion") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING)
                luaGD_hinterror(L, "suggestion", "string");

            String hint_string = luascript_stringhintlist(L, 2);

            prop->property.hint = PROPERTY_HINT_ENUM_SUGGESTION;
            prop->property.hint_string = hint_string;
        } else if (strcmp(key, "Flags") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "flags", "integer");

            String hint_string = luascript_stringhintlist(L, 2);

            prop->property.hint = PROPERTY_HINT_FLAGS;
            prop->property.hint_string = hint_string;
        } else if (strcmp(key, "File") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING)
                luaGD_hinterror(L, "file", "string");

            bool is_global = luaL_optboolean(L, 2, false);

            if (LuaStackOp<String>::is(L, 3))
                prop->property.hint_string = luascript_stringhintlist(L, 3);

            prop->property.hint = is_global ? PROPERTY_HINT_GLOBAL_FILE : PROPERTY_HINT_FILE;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Dir") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING)
                luaGD_hinterror(L, "directory", "string");

            bool is_global = luaL_optboolean(L, 2, false);

            prop->property.hint = is_global ? PROPERTY_HINT_GLOBAL_DIR : PROPERTY_HINT_DIR;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Multiline") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING)
                luaGD_hinterror(L, "multiline text", "string");

            prop->property.hint = PROPERTY_HINT_MULTILINE_TEXT;
            prop->property.hint_string = String();
        } else if (strcmp(key, "TextPlaceholder") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_STRING)
                luaGD_hinterror(L, "placeholder text", "string");

            String placeholder = LuaStackOp<String>::check(L, 2);

            prop->property.hint = PROPERTY_HINT_PLACEHOLDER_TEXT;
            prop->property.hint_string = placeholder;
        } else if (strcmp(key, "Flags2DRenderLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "2D render layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_2D_RENDER;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Flags2DPhysicsLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "2D physics layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_2D_PHYSICS;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Flags2DNavigationLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "2D navigation layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_2D_NAVIGATION;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Flags3DRenderLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "3D render layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_3D_RENDER;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Flags3DPhysicsLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "3D physics layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_3D_PHYSICS;
            prop->property.hint_string = String();
        } else if (strcmp(key, "Flags3DNavigationLayers") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_INT)
                luaGD_hinterror(L, "3D navigation layers", "integer");

            prop->property.hint = PROPERTY_HINT_LAYERS_3D_NAVIGATION;
            prop->property.hint_string = String();
        } else if (strcmp(key, "ExpEasing") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_FLOAT)
                luaGD_hinterror(L, "exp easing", "float");

            prop->property.hint = PROPERTY_HINT_EXP_EASING;
            prop->property.hint_string = String();
        } else if (strcmp(key, "NoAlpha") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_COLOR)
                luaGD_hinterror(L, "no alpha", "color");

            prop->property.hint = PROPERTY_HINT_COLOR_NO_ALPHA;
            prop->property.hint_string = String();
        } else if (strcmp(key, "TypedArray") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_ARRAY)
                luaGD_hinterror(L, "typed array", "array");

            const char *type = luaL_checkstring(L, 2);
            bool is_resource = luaL_optboolean(L, 3, false);

            prop->property.hint = PROPERTY_HINT_ARRAY_TYPE;

            if (is_resource) {
                // see core/object/object.h
                Array hint_values;
                hint_values.resize(3);
                hint_values[0] = Variant::OBJECT;
                hint_values[1] = PROPERTY_HINT_RESOURCE_TYPE;
                hint_values[2] = type;

                prop->property.hint_string = String("{0}/{1}:{2}").format(hint_values);
            } else {
                prop->property.hint_string = type;
            }
        } else if (strcmp(key, "Resource") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_OBJECT)
                luaGD_hinterror(L, "resource type", "object");

            const char *type = luaL_checkstring(L, 2);

            prop->property.hint = PROPERTY_HINT_RESOURCE_TYPE;
            prop->property.hint_string = type;
        } else if (strcmp(key, "NodePath") == 0) {
            if (prop->property.type != GDEXTENSION_VARIANT_TYPE_NODE_PATH)
                luaGD_hinterror(L, "node path valid types", "node path");

            String hint_string = luascript_stringhintlist(L, 2);

            prop->property.hint = PROPERTY_HINT_NODE_PATH_VALID_TYPES;
            prop->property.hint_string = hint_string;
        } else {
            luaGD_nomethoderror(L, key, "GDClassProperty");
        }

        lua_settop(L, 1); // return prop
        return 1;
    }

    luaGD_nonamecallatomerror(L);
}

/* EXPOSED FUNCTIONS */

void luascript_openlibs(lua_State *L) {
    // Class
    {
        luaL_newmetatable(L, CLASS_MT_NAME);

        lua_pushcfunction(L, luascript_gdclass_namecall, CLASS_MT_NAME ".__namecall");
        lua_setfield(L, -2, "__namecall");

        lua_pushcfunction(L, luascript_gdclass_newindex, CLASS_MT_NAME ".__newindex");
        lua_setfield(L, -2, "__newindex");

        lua_pushcfunction(L, luascript_gdclass_index, CLASS_MT_NAME ".__index");
        lua_setfield(L, -2, "__index");

        lua_setreadonly(L, -1, true);
        lua_pop(L, 1);

        lua_pushcfunction(L, luascript_gdclass, "_G.gdclass");
        lua_setglobal(L, "gdclass");
    }

    // Method
    {
        luaL_newmetatable(L, METHOD_MT_NAME);

        lua_pushcfunction(L, luascript_method_namecall, METHOD_MT_NAME ".__namecall");
        lua_setfield(L, -2, "__namecall");

        lua_setreadonly(L, -1, true);
        lua_pop(L, 1);
    }

    // Class property
    {
        luaL_newmetatable(L, CLASS_PROPERTY_MT_NAME);

        lua_pushcfunction(L, luascript_classprop_namecall, CLASS_PROPERTY_MT_NAME ".__namecall");
        lua_setfield(L, -2, "__namecall");

        lua_setreadonly(L, -1, true);
        lua_pop(L, 1);
    }
}
