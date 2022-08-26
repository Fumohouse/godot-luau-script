#pragma once

#include <lua.h>
#include <godot_cpp/classes/object.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

// Specialized templates are generated in luagd_ptrcall.gen.h.

template <typename T>
class LuaPtrcallArg
{
public:
    static bool get(lua_State *L, int index, T *ptr)
    {
        if (!LuaStackOp<T>::is(L, index))
            return false;

        *ptr = LuaStackOp<T>::get(L, index);

        return true;
    }

    static T check(lua_State *L, int index)
    {
        return LuaStackOp<T>::check(L, index);
    }
};

template <typename T>
class LuaPtrcallArg<T *>
{
public:
    // Variant types
    static bool get(lua_State *L, int index, T **ptr)
    {
        if (!LuaStackOp<T>::is(L, index))
            return false;

        *ptr = LuaStackOp<T>::get_ptr(L, index);

        return true;
    }

    static T *check(lua_State *L, int index)
    {
        return LuaStackOp<T>::check_ptr(L, index);
    }

    // Objects
    static bool get_obj(lua_State *L, int index, void **ptr)
    {
        if (!LuaStackOp<Object *>::is(L, index))
            return false;

        Object *obj = LuaStackOp<Object *>::get(L, index);
        *ptr = obj->_owner;

        return true;
    }

    static void *check_obj(lua_State *L, int index)
    {
        Object *ptr = LuaStackOp<Object *>::check(L, index);
        if (ptr == nullptr)
            return nullptr;

        return ptr->_owner;
    }
};
