#pragma once

#include <godot/gdnative_interface.h>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include "luagd_stack.h"

using namespace godot;

struct lua_State;

struct GDProperty
{
    GDNativeVariantType type = GDNATIVE_VARIANT_TYPE_NIL;
    uint32_t usage = PROPERTY_USAGE_DEFAULT;

    String name;
    StringName class_name;

    uint32_t hint = PROPERTY_HINT_NONE;
    String hint_string;

    operator Dictionary() const;
    operator Variant() const;
};

struct GDClassProperty
{
    GDProperty property;

    StringName getter;
    StringName setter;

    Variant default_value;
};

struct GDMethod
{
    String name;
    GDProperty return_val;
    uint32_t flags = METHOD_FLAGS_DEFAULT;
    List<GDProperty> arguments;
    List<Variant> default_arguments;

    operator Dictionary() const;
    operator Variant() const;
};

struct GDClassDefinition
{
    String name;
    String extends;

    bool is_tool;

    HashMap<StringName, GDMethod> methods;
    HashMap<StringName, GDClassProperty> properties;
};

struct GDClassMethods
{
    int initialize = -1;

    HashMap<int, int> notifications;
    HashMap<StringName, int> methods;
};

template class LuaStackOp<GDProperty>;
template class LuaStackOp<GDClassDefinition>;
template class LuaStackOp<GDClassMethods>;

void luascript_openlibs(lua_State *L);
void luascript_openclasslib(lua_State *L, bool load_methods);
