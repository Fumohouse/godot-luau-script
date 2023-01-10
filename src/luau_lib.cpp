#include "luau_lib.h"

#include <gdextension_interface.h>
#include <lualib.h>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "lua.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
#include "luau_script.h"

using namespace godot;

#define PROPERTY_MT_NAME "Luau.GDProperty"

UDATA_STACK_OP_IMPL(GDProperty, PROPERTY_MT_NAME, DTOR(GDProperty))

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

/* PROPERTY */

static int luascript_gdproperty(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    GDProperty property;

    if (luaGD_getfield(L, 1, "type"))
        property.type = static_cast<GDExtensionVariantType>(luaGD_checkvaluetype<uint32_t>(L, -1, "type", LUA_TNUMBER));

    if (luaGD_getfield(L, 1, "name"))
        property.name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "hint"))
        property.hint = luaGD_checkvaluetype<uint32_t>(L, -1, "hint", LUA_TNUMBER);

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

/* EXPOSED FUNCTIONS */

void luascript_openlibs(lua_State *L) {
    luaL_newmetatable(L, PROPERTY_MT_NAME);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    lua_pushcfunction(L, luascript_gdproperty, "_G.gdproperty");
    lua_setglobal(L, "gdproperty");
}

#define READ_MAP(table_key, expr)                                                           \
    if (luaGD_getfield(L, idx, table_key)) {                                                \
        if (lua_type(L, -1) != LUA_TTABLE)                                                  \
            luaGD_valueerror(L, table_key, luaGD_typename(L, -1), "table");                 \
                                                                                            \
        lua_pushnil(L);                                                                     \
                                                                                            \
        while (lua_next(L, -2) != 0) {                                                      \
            if (!LuaStackOp<String>::is(L, -2))                                             \
                luaGD_keyerror(L, table_key " table", luaGD_typename(L, -2), "string");     \
                                                                                            \
            String key = LuaStackOp<String>::get(L, -2);                                    \
                                                                                            \
            if (lua_type(L, -1) != LUA_TTABLE)                                              \
                luaGD_valueerror(L, key.utf8().get_data(), luaGD_typename(L, -1), "table"); \
                                                                                            \
            expr;                                                                           \
                                                                                            \
            lua_pop(L, 1); /* value in this iteration */                                    \
        }                                                                                   \
                                                                                            \
        lua_pop(L, 1); /* table */                                                          \
    }

GDClassDefinition luascript_read_class(lua_State *L, int idx, const String &path) {
    GDClassDefinition def;

    if (luaGD_getfield(L, idx, "name"))
        def.name = luaGD_checkvaluetype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "extends"))
        def.extends = luaGD_checkvaluetype<String>(L, -1, "extends", LUA_TSTRING);
    else
        def.extends = "RefCounted";

    if (luaGD_getfield(L, idx, "permissions")) {
        if (path.is_empty() || LuauLanguage::get_singleton() == nullptr || !LuauLanguage::get_singleton()->is_core_script(path))
            luaL_error(L, "!!! cannot set permissions on a non-core script !!!");

        def.permissions = static_cast<ThreadPermissions>(luaGD_checkvaluetype<int32_t>(L, -1, "permissions", LUA_TNUMBER));
    }

    if (luaGD_getfield(L, idx, "tool"))
        def.is_tool = luaGD_checkvaluetype<bool>(L, -1, "tool", LUA_TBOOLEAN);

    READ_MAP("methods", {
        GDMethod method = luascript_read_method(L, -1);
        method.name = key;

        def.methods[key] = method;
    });

    READ_MAP("properties", {
        def.properties[key] = luascript_read_class_property(L, -1);
    });

    READ_MAP("signals", {
        // Signals are stored as methods with only name and arguments
        GDMethod signal;
        signal.name = key;

        luascript_read_args(L, -1, signal.arguments);

        def.signals[key] = signal;
    });

    READ_MAP("rpcs", {
        GDRpc rpc = luascript_read_rpc(L, -1);
        rpc.name = key;

        def.rpcs[key] = rpc;
    });

    return def;
}
