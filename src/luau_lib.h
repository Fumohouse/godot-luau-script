#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
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
#include "utils.h"
#include "wrapped_no_binding.h"

using namespace godot;

#define LUASCRIPT_MODULE_TABLE "_MODULES"
#define LUASCRIPT_MT_SCRIPT "__script"

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

	void set_variant_type() {
		type = GDEXTENSION_VARIANT_TYPE_NIL;
		usage = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT;
	}

	void set_object_type(const String &p_type, const String &p_base_type = "") {
		type = GDEXTENSION_VARIANT_TYPE_OBJECT;
		class_name = p_type; // For non-property usage

		const String &godot_type = p_base_type.is_empty() ? p_type : p_base_type;

		if (nb::ClassDB::get_singleton_nb()->is_parent_class(godot_type, "Resource")) {
			hint = PROPERTY_HINT_RESOURCE_TYPE;
			hint_string = p_type;
		} else if (nb::ClassDB::get_singleton_nb()->is_parent_class(godot_type, "Node")) {
			hint = PROPERTY_HINT_NODE_TYPE;
			hint_string = p_type;
		}
	}

	void set_typed_array_type(const GDProperty &p_type) {
		type = GDEXTENSION_VARIANT_TYPE_ARRAY;
		hint = PROPERTY_HINT_ARRAY_TYPE;

		if (p_type.type == GDEXTENSION_VARIANT_TYPE_OBJECT) {
			if (p_type.hint == PROPERTY_HINT_RESOURCE_TYPE) {
				hint_string = Utils::resource_type_hint(p_type.hint_string);
			} else {
				hint_string = p_type.class_name;
			}
		} else {
			hint_string = Variant::get_type_name(Variant::Type(p_type.type));
		}
	}
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
	HashMap<StringName, int> constants;

	int set_prop(const String &p_name, const GDClassProperty &p_prop);
};

void luascript_get_classdef_or_type(lua_State *L, int p_index, String &r_type, LuauScript *&r_script);
String luascript_get_scriptname_or_type(lua_State *L, int p_index, LuauScript **r_script = nullptr);

GDProperty luascript_read_property(lua_State *L, int p_idx);
void luascript_openlibs(lua_State *L);

LuauScript *luascript_class_table_get_script(lua_State *L, int p_i);
