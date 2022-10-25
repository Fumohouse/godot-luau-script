#include "luau_lib.h"

#include <lualib.h>
#include <cstring>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "luagd_utils.h"

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

using namespace godot;

#define PROPERTY_MT_NAME "Luau.GDProperty"
#define CLASS_MT_NAME "Luau.GDClassDefinition"
#define METHODS_MT_NAME "Luau.GDClassDefinition.Methods"

LUA_UDATA_STACK_OP(GDProperty, PROPERTY_MT_NAME, DTOR(GDProperty))
LUA_UDATA_STACK_OP(GDClassDefinition, CLASS_MT_NAME, DTOR(GDClassDefinition))
LUA_UDATA_STACK_OP(GDClassMethods, METHODS_MT_NAME, DTOR(GDClassMethods))

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
        property.type = luaGD_checkkeytype<uint32_t>(L, -1, "type", LUA_TNUMBER);

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

/* CLASS
    There are two cases in which we would want information on a class:
    1. On initial LuauScript resource load, when only VM-agnostic info is loaded (i.e. not functions)
    2. When the resource has already loaded, and we need to store all the functions that may need to be called, on a per-VM basis

    Hence the two definitions of everything.
 */

static int luascript_gdclass(lua_State *L)
{
    GDClassDefinition def;

    def.name = luaL_checkstring(L, 1);

    if (lua_gettop(L) >= 2)
        def.extends = luaL_checkstring(L, 2);
    else
        def.extends = "RefCounted";

    LuaStackOp<GDClassDefinition>::push(L, def);
    return 1;
}

static int luascript_gdclass_methods(lua_State *L)
{
    // ensure correct invocation
    luaL_checkstring(L, 1);
    if (lua_gettop(L) >= 2)
        luaL_checkstring(L, 2);

    LuaStackOp<GDClassMethods>::push(L, GDClassMethods());
    return 1;
}

static int luascript_gdclass_namecall(lua_State *L)
{
    if (const char *name = lua_namecallatom(L, nullptr))
    {
        GDClassDefinition *def = LuaStackOp<GDClassDefinition>::check_ptr(L, 1);

        // no-op methods
        if (strcmp(name, "Initialize") == 0)
        {
            luaL_checktype(L, 2, LUA_TFUNCTION);

            return 0;
        }

        if (strcmp(name, "Subscribe") == 0)
        {
            luaL_checkinteger(L, 2);
            luaL_checktype(L, 3, LUA_TFUNCTION);

            return 0;
        }

        // Defined methods
        if (strcmp(name, "Tool") == 0)
        {
            def->is_tool = luaL_checkboolean(L, 2);
            return 0;
        } // :Tool

        if (strcmp(name, "RegisterMethod") == 0)
        {
            const char *method_name = luaL_checkstring(L, 2);
            luaL_checktype(L, 3, LUA_TFUNCTION); // ensure correct invocation

            if (lua_gettop(L) < 4)
            {
                def->methods.insert(method_name, GDMethod());
                return 0;
            }

            luaL_checktype(L, 4, LUA_TTABLE);

            GDMethod method;

            method.name = method_name;

            if (luaGD_getfield(L, 4, "args"))
            {
                luaL_checktype(L, -1, LUA_TTABLE);

                int args_len = lua_objlen(L, -1);
                for (int i = 1; i <= args_len; i++)
                {
                    lua_rawgeti(L, -1, i);

                    GDProperty *prop = LuaStackOp<GDProperty>::check_ptr(L, -1);
                    method.arguments.push_back(*prop);

                    lua_pop(L, 1); // rawgeti
                }

                lua_pop(L, 1); // args
            }

            if (luaGD_getfield(L, 4, "defaultArgs"))
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

            if (luaGD_getfield(L, 4, "returnVal"))
            {
                GDProperty *return_val = LuaStackOp<GDProperty>::check_ptr(L, -1);
                method.return_val = *return_val;

                lua_pop(L, 1); // returnVal
            }

            if (luaGD_getfield(L, 4, "flags"))
                method.flags = luaGD_checkkeytype<uint32_t>(L, -1, "flags", LUA_TNUMBER);

            def->methods.insert(method_name, method);

            return 0;
        } // :RegisterMethod

        if (strcmp(name, "RegisterProperty") == 0)
        {
            GDProperty *property = LuaStackOp<GDProperty>::check_ptr(L, 2);
            const char *getter = luaL_checkstring(L, 3);
            const char *setter = luaL_checkstring(L, 4);

            GDClassProperty prop;
            prop.property = *property;
            prop.getter = getter;
            prop.setter = setter;

            if (lua_gettop(L) >= 5)
                prop.default_value = LuaStackOp<Variant>::get(L, 5);

            def->properties.insert(property->name, prop);

            return 0;
        } // :RegisterProperty

        luaL_error(L, "%s is not a valid member of " CLASS_MT_NAME, name);
    }

    luaL_error(L, "no namecallatom");
}

static int luascript_gdclass_methods_namecall(lua_State *L)
{
    if (const char *name = lua_namecallatom(L, nullptr))
    {
        GDClassMethods *methods = LuaStackOp<GDClassMethods>::check_ptr(L, 1);

        // no-op methods
        if (strcmp(name, "Tool") == 0)
        {
            luaL_checkboolean(L, 2);

            return 0;
        }

        if (strcmp(name, "RegisterProperty") == 0)
        {
            LuaStackOp<GDProperty>::check(L, 2);
            luaL_checkstring(L, 3);
            luaL_checkstring(L, 4);
            if (lua_gettop(L) >= 5)
                LuaStackOp<Variant>::get(L, 5);

            return 0;
        }

        // Defined methods
        if (strcmp(name, "Initialize") == 0)
        {
            luaL_checktype(L, 2, LUA_TFUNCTION);
            methods->initialize = lua_ref(L, 2);

            return 0;
        } // :Initialize

        if (strcmp(name, "Subscribe") == 0)
        {
            int what = luaL_checkinteger(L, 2);
            luaL_checktype(L, 3, LUA_TFUNCTION);

            methods->notifications.insert(what, lua_ref(L, 3));

            return 0;
        } // :Subscribe

        if (strcmp(name, "RegisterMethod") == 0)
        {
            const char *name = luaL_checkstring(L, 2);
            luaL_checktype(L, 3, LUA_TFUNCTION);
            if (lua_gettop(L) >= 4)
                luaL_checktype(L, 4, LUA_TTABLE); // ensure correct invocation

            methods->methods.insert(name, lua_ref(L, 3));

            return 0;
        } // :RegisterMethod

        luaL_error(L, "%s is not a valid member of " METHODS_MT_NAME, name);
    }

    luaL_error(L, "no namecallatom");
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

void luascript_openclasslib(lua_State *L, bool load_methods)
{
    if (load_methods)
    {
        lua_pushcfunction(L, luascript_gdclass_methods, "_G.gdclass[methods]");
        lua_setglobal(L, "gdclass");

        if (luaL_newmetatable(L, METHODS_MT_NAME))
        {
            lua_pushcfunction(L, luascript_gdclass_methods_namecall, METHODS_MT_NAME ".__namecall");
            lua_setfield(L, -2, "__namecall");

            lua_setreadonly(L, -1, true);
        }

        lua_pop(L, 1);
    }
    else
    {
        lua_pushcfunction(L, luascript_gdclass, "_G.gdclass[nomethods]");
        lua_setglobal(L, "gdclass");

        if (luaL_newmetatable(L, CLASS_MT_NAME))
        {
            lua_pushcfunction(L, luascript_gdclass_namecall, CLASS_MT_NAME ".__namecall");
            lua_setfield(L, -2, "__namecall");

            lua_setreadonly(L, -1, true);
        }

        lua_pop(L, 1);
    }
}
