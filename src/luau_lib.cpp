#include "luau_lib.h"

#include <lualib.h>
#include <cstring>
#include <godot/gdnative_interface.h>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "luagd_utils.h"

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

using namespace godot;

#define PROPERTY_MT_NAME "Luau.GDProperty"

LUA_UDATA_STACK_OP(GDProperty, PROPERTY_MT_NAME, DTOR(GDProperty))

/* STRUCTS */

GDProperty::operator Dictionary() const
{
    Dictionary dict;

    dict["type"] = type;
    dict["usage"] = usage;

    dict["name"] = name;
    dict["class_name"] = class_name;

    dict["hint"] = hint;
    dict["hint_string"] = hint_string;

    return dict;
}

GDProperty::operator Variant() const
{
    return this->operator Dictionary();
}

GDMethod::operator Dictionary() const
{
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

GDMethod::operator Variant() const
{
    return this->operator Dictionary();
}

/* PROPERTY */

static int luascript_gdproperty(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    GDProperty property;

    if (luaGD_getfield(L, 1, "type"))
        property.type = static_cast<GDNativeVariantType>(luaGD_checkkeytype<uint32_t>(L, -1, "type", LUA_TNUMBER));

    if (luaGD_getfield(L, 1, "name"))
        property.name = luaGD_checkkeytype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "hint"))
        property.hint = luaGD_checkkeytype<uint32_t>(L, -1, "hint", LUA_TNUMBER);

    if (luaGD_getfield(L, 1, "hintString"))
        property.hint_string = luaGD_checkkeytype<String>(L, -1, "hintString", LUA_TSTRING);

    if (luaGD_getfield(L, 1, "usage"))
        property.usage = luaGD_checkkeytype<uint32_t>(L, -1, "usage", LUA_TNUMBER);

    if (luaGD_getfield(L, 1, "className"))
        property.class_name = luaGD_checkkeytype<String>(L, -1, "className", LUA_TSTRING);

    LuaStackOp<GDProperty>::push(L, property);
    return 1;
}

static GDMethod luascript_read_method(lua_State *L, int idx)
{
    GDMethod method;

    if (luaGD_getfield(L, idx, "args"))
    {
        luaL_checktype(L, -1, LUA_TTABLE);

        int args_len = lua_objlen(L, -1);
        for (int i = 1; i <= args_len; i++)
        {
            lua_rawgeti(L, -1, i);

            method.arguments.push_back(LuaStackOp<GDProperty>::check(L, -1));

            lua_pop(L, 1); // rawgeti
        }

        lua_pop(L, 1); // args
    }

    if (luaGD_getfield(L, idx, "defaultArgs"))
    {
        luaL_checktype(L, -1, LUA_TTABLE);

        int default_args_len = lua_objlen(L, -1);
        for (int i = 1; i <= default_args_len; i++)
        {
            lua_rawgeti(L, -1, i);

            method.default_arguments.push_back(LuaStackOp<Variant>::get(L, -1));

            lua_pop(L, 1); // rawgeti
        }

        lua_pop(L, 1); // defaultArgs
    }

    if (luaGD_getfield(L, idx, "returnVal"))
    {
        method.return_val = LuaStackOp<GDProperty>::check(L, -1);
        lua_pop(L, 1);
    }

    if (luaGD_getfield(L, idx, "flags"))
        method.flags = luaGD_checkkeytype<uint32_t>(L, -1, "flags", LUA_TNUMBER);

    return method;
}

static GDClassProperty luascript_read_class_property(lua_State *L, int idx)
{
    GDClassProperty property;

    if (luaGD_getfield(L, idx, "property"))
    {
        property.property = LuaStackOp<GDProperty>::check(L, -1);
        lua_pop(L, 1);
    }
    else
    {
        luaL_error(L, "missing 'property' in class property definition");
    }

    if (luaGD_getfield(L, idx, "getter"))
        property.getter = luaGD_checkkeytype<String>(L, -1, "getter", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "setter"))
        property.setter = luaGD_checkkeytype<String>(L, -1, "setter", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "default"))
    {
        property.default_value = LuaStackOp<Variant>::get(L, -1);
        lua_pop(L, 1);
    }

    return property;
}

/* EXPOSED FUNCTIONS */

void luascript_openlibs(lua_State *L)
{
    luaL_newmetatable(L, PROPERTY_MT_NAME);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    lua_pushcfunction(L, luascript_gdproperty, "_G.gdproperty");
    lua_setglobal(L, "gdproperty");
}

GDClassDefinition luascript_read_class(lua_State *L, int idx)
{
    GDClassDefinition def;

    if (luaGD_getfield(L, idx, "name"))
        def.name = luaGD_checkkeytype<String>(L, -1, "name", LUA_TSTRING);

    if (luaGD_getfield(L, idx, "extends"))
        def.extends = luaGD_checkkeytype<String>(L, -1, "extends", LUA_TSTRING);
    else
        def.extends = "RefCounted";

    if (luaGD_getfield(L, idx, "tool"))
        def.is_tool = luaGD_checkkeytype<bool>(L, -1, "tool", LUA_TBOOLEAN);

    if (luaGD_getfield(L, idx, "methods"))
    {
        luaL_checktype(L, -1, LUA_TTABLE);

        lua_pushnil(L);

        while (lua_next(L, -2) != 0)
        {
            String method_name = LuaStackOp<String>::check(L, -2);
            luaL_checktype(L, -1, LUA_TTABLE);

            GDMethod method = luascript_read_method(L, -1);
            method.name = method_name;

            def.methods[method_name] = method;

            lua_pop(L, 1); // value in this iteration
        }

        lua_pop(L, 1); // methods
    }

    if (luaGD_getfield(L, idx, "properties"))
    {
        luaL_checktype(L, -1, LUA_TTABLE);

        lua_pushnil(L);

        while (lua_next(L, -2) != 0)
        {
            String property_name = LuaStackOp<String>::check(L, -2);
            luaL_checktype(L, -1, LUA_TTABLE);

            def.properties[property_name] = luascript_read_class_property(L, -1);

            lua_pop(L, 1); // value in this iteration
        }

        lua_pop(L, 1); // properties
    }

    return def;
}
