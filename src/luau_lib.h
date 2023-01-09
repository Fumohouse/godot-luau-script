#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_permissions.h"
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
    List<GDProperty> arguments;
    List<Variant> default_arguments;

    operator Dictionary() const;
    operator Variant() const;
};

struct GDClassDefinition {
    String name;
    String extends;

    ThreadPermissions permissions = PERMISSION_BASE;

    bool is_tool;

    HashMap<StringName, GDMethod> methods;
    HashMap<StringName, GDClassProperty> properties;
};

template class LuaStackOp<GDProperty>;

void luascript_openlibs(lua_State *L);
GDClassDefinition luascript_read_class(lua_State *L, int idx, const String &path = "");
