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

#define PROPERTY_MT_NAME "Luau.GDProperty"
#define CLASS_MT_NAME "Luau.GDClassDefinition"

UDATA_STACK_OP_IMPL(GDProperty, PROPERTY_MT_NAME, DTOR(GDProperty))
UDATA_STACK_OP_IMPL(GDClassDefinition, CLASS_MT_NAME, DTOR(GDClassDefinition))

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
    return this->operator Dictionary();
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
    return this->operator Dictionary();
}

void GDClassDefinition::set_prop(const String &name, const GDClassProperty &prop) {
    HashMap<StringName, uint64_t>::ConstIterator E = property_indices.find(name);

    if (E) {
        properties.set(E->value, prop);
    } else {
        property_indices[name] = properties.size();
        properties.push_back(prop);
    }
}

/* PROPERTY */

static int luascript_gdproperty(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    GDProperty property;

    if (luaGD_getfield(L, 1, "type"))
        property.type = static_cast<GDExtensionVariantType>(luaGD_checkvaluetype<uint32_t>(L, -1, "type", LUA_TNUMBER));

    if (luaGD_getfield(L, 1, "name"))
        property.name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "hint"))
        property.hint = static_cast<PropertyHint>(luaGD_checkvaluetype<uint32_t>(L, -1, "hint", LUA_TNUMBER));

    if (luaGD_getfield(L, 1, "hintString"))
        property.hint_string = luaGD_checkvaluetype<String>(L, -1, "hintString", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "usage"))
        property.usage = luaGD_checkvaluetype<uint32_t>(L, -1, "usage", LUA_TNUMBER);

    if (luaGD_getfield(L, 1, "className"))
        property.class_name = luaGD_checkvaluetype<String>(L, -1, "className", LUA_TSTRING);

    LuaStackOp<GDProperty>::push(L, property);
    return 1;
}

static void luascript_read_args(lua_State *L, int idx, Vector<GDProperty> &arguments) {
    if (luaGD_getfield(L, idx, "args")) {
        if (lua_type(L, -1) != LUA_TTABLE)
            luaGD_valueerror(L, "args", luaGD_typename(L, -1), "table");

        int args_len = lua_objlen(L, -1);
        arguments.resize(args_len);

        for (int i = 1; i <= args_len; i++) {
            lua_rawgeti(L, -1, i);

            if (!LuaStackOp<GDProperty>::is(L, -1))
                luaGD_arrayerror(L, "args", luaGD_typename(L, -1), "GDProperty");

            arguments.set(i - 1, LuaStackOp<GDProperty>::get(L, -1));

            lua_pop(L, 1); // rawgeti
        }

        lua_pop(L, 1); // args
    }
}

static GDMethod luascript_read_method(lua_State *L, int idx) {
    GDMethod method;

    luascript_read_args(L, idx, method.arguments);

    if (luaGD_getfield(L, idx, "defaultArgs")) {
        if (lua_type(L, -1) != LUA_TTABLE)
            luaGD_valueerror(L, "defaultArgs", luaGD_typename(L, -1), "table");

        int default_args_len = lua_objlen(L, -1);
        for (int i = 1; i <= default_args_len; i++) {
            lua_rawgeti(L, -1, i);

            method.default_arguments.push_back(LuaStackOp<Variant>::get(L, -1));

            lua_pop(L, 1); // rawgeti
        }

        lua_pop(L, 1); // defaultArgs
    }

    if (luaGD_getfield(L, idx, "returnVal")) {
        if (!LuaStackOp<GDProperty>::is(L, -1))
            luaGD_valueerror(L, "returnVal", luaGD_typename(L, -1), "GDProperty");

        method.return_val = LuaStackOp<GDProperty>::get(L, -1);
        lua_pop(L, 1);
    }

    if (luaGD_getfield(L, idx, "flags"))
        method.flags = luaGD_checkvaluetype<uint32_t>(L, -1, "flags", LUA_TNUMBER);

    return method;
}

static GDClassProperty luascript_read_class_property(lua_State *L, int idx) {
    GDClassProperty property;

    if (luaGD_getfield(L, idx, "property")) {
        if (!LuaStackOp<GDProperty>::is(L, -1))
            luaGD_valueerror(L, "property", luaGD_typename(L, -1), "GDProperty");

        property.property = LuaStackOp<GDProperty>::get(L, -1);
        lua_pop(L, 1);
    } else {
        luaL_error(L, "missing 'property' in class property definition");
    }

    if (luaGD_getfield(L, idx, "getter"))
        property.getter = luaGD_checkvaluetype<String>(L, -1, "getter", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "setter"))
        property.setter = luaGD_checkvaluetype<String>(L, -1, "setter", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "default")) {
        property.default_value = LuaStackOp<Variant>::get(L, -1);
        lua_pop(L, 1);
    }

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

const char *gdclass_table_keys[] = {
    "name",
    "extends",
    "permissions",
    "tool"
};

constexpr uint64_t gdclass_table_keys_len = sizeof(gdclass_table_keys) / sizeof(const char *);

static int luascript_gdclass(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::alloc(L);

    if (luaGD_getfield(L, 1, "name"))
        def->name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "extends"))
        def->extends = luaGD_checkvaluetype<String>(L, -1, "extends", LUA_TSTRING);
    else
        def->extends = "RefCounted";

    if (luaGD_getfield(L, 1, "permissions")) {
        GDThreadData *udata = luaGD_getthreaddata(L);

        if (udata->path.is_empty() || LuauLanguage::get_singleton() == nullptr || !LuauLanguage::get_singleton()->is_core_script(udata->path))
            luaL_error(L, "!!! cannot set permissions on a non-core script !!!");

        def->permissions = static_cast<ThreadPermissions>(luaGD_checkvaluetype<int32_t>(L, -1, "permissions", LUA_TNUMBER));
    }

    if (luaGD_getfield(L, 1, "tool"))
        def->is_tool = luaGD_checkvaluetype<bool>(L, -1, "tool", LUA_TBOOLEAN);

    def->table_ref = lua_ref(L, 1);

    return 1;
}

static int luascript_gdclass_namecall(lua_State *L) {
    if (const char *key = lua_namecallatom(L, nullptr)) {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);

        // "Raw" functions
        if (strcmp(key, "RegisterMethod") == 0) {
            String name = LuaStackOp<String>::check(L, 2);
            luaL_checktype(L, 3, LUA_TTABLE);

            GDMethod method = luascript_read_method(L, 3);
            method.name = name;

            def->methods[name] = method;

            return 0;
        }

        if (strcmp(key, "RegisterProperty") == 0) {
            String name = LuaStackOp<String>::check(L, 2);
            luaL_checktype(L, 3, LUA_TTABLE);

            GDClassProperty prop = luascript_read_class_property(L, 3);
            prop.property.name = name;

            def->set_prop(name, prop);

            return 0;
        }

        if (strcmp(key, "RegisterSignal") == 0) {
            String name = LuaStackOp<String>::check(L, 2);
            luaL_checktype(L, 3, LUA_TTABLE);

            // Signals are stored as methods with only name and arguments
            GDMethod signal;
            signal.name = name;
            luascript_read_args(L, 3, signal.arguments);

            def->signals[name] = signal;

            return 0;
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

    luaL_error(L, "no namecallatom");
}

static int luascript_gdclass_newindex(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    const char *key = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    for (int i = 0; i < gdclass_table_keys_len; i++) {
        if (strcmp(key, gdclass_table_keys[i]) == 0) {
            luaL_error(L, "cannot set '%s' on GDClassDefinition: key is reserved", key);
        }
    }

    ERR_FAIL_COND_V_MSG(def->table_ref < 0, 0, "Failed to set method on class definition: table ref is invalid");

    lua_getref(L, def->table_ref);
    lua_insert(L, -3);
    lua_settable(L, -3);

    return 0;
}

static int luascript_gdclass_index(lua_State *L) {
    GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);
    luaL_checkstring(L, 2);

    lua_getref(L, def->table_ref);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);

    return 1;
}

/* EXPOSED FUNCTIONS */

void luascript_openlibs(lua_State *L) {
    // Property
    {
        luaL_newmetatable(L, PROPERTY_MT_NAME);
        lua_setreadonly(L, -1, true);
        lua_pop(L, 1);

        lua_pushcfunction(L, luascript_gdproperty, "_G.gdproperty");
        lua_setglobal(L, "gdproperty");
    }

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
}
