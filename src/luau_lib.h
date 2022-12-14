#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd.h"
#include "luagd_stack.h"

using namespace godot;

struct lua_State;

struct GDProperty {
    GDExtensionVariantType type = GDEXTENSION_VARIANT_TYPE_NIL;
    uint32_t usage = PROPERTY_USAGE_DEFAULT;

    String name;
    StringName class_name;

    uint32_t hint = PROPERTY_HINT_NONE;
    String hint_string;

    operator Dictionary() const;
    operator Variant() const;
};

struct GDClassProperty {
    GDProperty property;

    StringName getter;
    StringName setter;

    Variant default_value;
};

struct GDMethod {
    String name;
    GDProperty return_val;
    uint32_t flags = METHOD_FLAGS_DEFAULT;
    Vector<GDProperty> arguments;
    Vector<Variant> default_arguments;

    operator Dictionary() const;
    operator Variant() const;
};

// ! Reference: modules/multiplayer/scene_rpc_interface.cpp _parse_rpc_config
struct GDRpc {
    String name;
    MultiplayerAPI::RPCMode rpc_mode = MultiplayerAPI::RPC_MODE_AUTHORITY;
    MultiplayerPeer::TransferMode transfer_mode = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    bool call_local = false;
    int channel = 0;

    operator Dictionary() const;
    operator Variant() const;
};

struct GDClassDefinition {
    String name;
    String extends;

    ThreadPermissions permissions = PERMISSION_BASE;

    bool is_tool = false;

    HashMap<StringName, GDMethod> methods;
    HashMap<StringName, GDClassProperty> properties;
    HashMap<StringName, GDMethod> signals;
    HashMap<StringName, GDRpc> rpcs;
    HashMap<StringName, Variant> constants;
};

STACK_OP_PTR_DEF(GDProperty)

void luascript_openlibs(lua_State *L);
GDClassDefinition luascript_read_class(lua_State *L, int idx, const String &path = "");
