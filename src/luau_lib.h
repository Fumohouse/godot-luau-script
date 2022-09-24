#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "luagd_stack.h"

using namespace godot;

struct lua_State;

struct GDProperty
{
    Dictionary internal;
};

struct GDClassProperty
{
    GDProperty property;

    StringName getter;
    StringName setter;

    Variant default_value;
};

struct GDClassDefinition
{
    String name;
    String extends;

    bool is_tool;

    HashMap<StringName, Dictionary> methods;
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
