#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_permissions.h"
#include "luagd_stack.h"

using namespace godot;

#define LUASCRIPT_MODULE_TABLE "_MODULES"

struct lua_State;

struct GDProperty {
    GDExtensionVariantType type = GDEXTENSION_VARIANT_TYPE_NIL;
    BitField<PropertyUsageFlags> usage = PROPERTY_USAGE_DEFAULT;

    String name;
    StringName class_name;

    PropertyHint hint = PROPERTY_HINT_NONE;
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
    BitField<MethodFlags> flags = METHOD_FLAGS_DEFAULT;
    Vector<GDProperty> arguments;
    Vector<Variant> default_arguments;

    bool is_signal = false;

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

class LuauScript;

struct GDClassDefinition {
    LuauScript *script = nullptr;
    int table_ref = -1;

    String name;
    String extends = "RefCounted";
    LuauScript *base_script = nullptr;

    String icon_path;

    ThreadPermissions permissions = PERMISSION_BASE;

    bool is_tool = false;

    HashMap<StringName, GDMethod> methods;
    HashMap<StringName, uint64_t> property_indices;
    Vector<GDClassProperty> properties;
    HashMap<StringName, GDMethod> signals;
    HashMap<StringName, GDRpc> rpcs;
    HashMap<StringName, Variant> constants;

    bool is_readonly = false;

    int set_prop(const String &name, const GDClassProperty &prop);
};

STACK_OP_PTR_DEF(GDClassDefinition)

class SignalWaiter : public Object {
    GDCLASS(SignalWaiter, Object)

    lua_State *L;
    int thread_ref;
    Signal signal;
    Callable callable;

protected:
    static void _bind_methods();

public:
    void initialize(lua_State *L, Signal p_signal);
    void on_signal(const Variant **p_args, GDExtensionInt p_argc, GDExtensionCallError &r_err);

    SignalWaiter() :
            callable(Callable(this, "on_signal")) {}
};

void luascript_get_classdef_or_type(lua_State *L, int index, String &r_type, LuauScript *&r_script);

GDProperty luascript_read_property(lua_State *L, int idx);
void luascript_openlibs(lua_State *L);
