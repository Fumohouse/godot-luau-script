#include "luau_lib.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <string.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
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
            luaGD_valueerror(L, "rpcMode", luaGD_typename(L, -1), "MultiplayerAPI.RPCMode");

        rpc.rpc_mode = (MultiplayerAPI::RPCMode)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    if (luaGD_getfield(L, idx, "transferMode")) {
        if (!lua_isnumber(L, -1))
            luaGD_valueerror(L, "rpcMode", luaGD_typename(L, -1), "MultiplayerPeer.TransferMode");

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

    // Create def table in registry.
    lua_newtable(L);
    def.table_ref = lua_ref(L, -1);
    lua_pop(L, 1); // table

    LuaStackOp<GDClassDefinition>::push(L, def);

    return 1;
}

static int luascript_gdclass_namecall(lua_State *L) {
    if (const char *key = lua_namecallatom(L, nullptr)) {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);

        // Chaining functions
        if (strcmp(key, "Tool") == 0) {
            def->is_tool = LuaStackOp<bool>::check(L, 2);

            lua_settop(L, 1); // return def
            return 1;
        }

        if (strcmp(key, "Permissions") == 0) {
            int32_t permissions = LuaStackOp<int32_t>::check(L, 2);

            GDThreadData *udata = luaGD_getthreaddata(L);

            if (udata->path.is_empty() || LuauLanguage::get_singleton() == nullptr || !LuauLanguage::get_singleton()->is_core_script(udata->path))
                luaL_error(L, "!!! cannot set permissions on a non-core script !!!");

            def->permissions = static_cast<ThreadPermissions>(permissions);

            lua_settop(L, 1); // return def
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
            GDProperty prop = luascript_read_property(L, 3);

            GDClassProperty class_prop;
            prop.name = name;
            class_prop.property = prop;

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

        luaGD_nomethoderror(L, key, "GDClassDefinition");
    }

    luaGD_nonamecallatomerror(L);
}

static int luascript_gdclass_newindex(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    const char *key = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    ERR_FAIL_COND_V_MSG(def->table_ref < 0, 0, "Failed to set method on class definition: table ref is invalid");

    lua_getref(L, def->table_ref);
    lua_insert(L, -3);
    lua_settable(L, -3);

    return 0;
}

static int luascript_gdclass_index(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    luaL_checkstring(L, 2);

    ERR_FAIL_COND_V_MSG(def->table_ref < 0, 0, "Failed to get method on class definition: table ref is invalid");

    lua_getref(L, def->table_ref);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);

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

static int luascript_classprop_namecall(lua_State *L) {
    GDClassProperty *prop = LuaStackOp<GDClassProperty *>::check(L, 1);

    if (const char *key = lua_namecallatom(L, nullptr)) {
        if (strcmp(key, "Default") == 0) {
            prop->default_value = LuaStackOp<Variant>::check(L, 2);
        } else if (strcmp(key, "SetGet") == 0) {
            prop->setter = luaL_optstring(L, 2, "");
            prop->getter = luaL_optstring(L, 3, "");
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
