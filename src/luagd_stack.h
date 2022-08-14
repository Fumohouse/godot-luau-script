#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;

template <typename T>
class LuaStackOp
{
public:
    static void push(lua_State *L, const T &value);

    static T *get_ptr(lua_State *L, int index);
    static T get(lua_State *L, int index);

    static bool is(lua_State *L, int index);

    static T *check_ptr(lua_State *L, int index);
    static T check(lua_State *L, int index);
};

template class LuaStackOp<bool>;
template class LuaStackOp<int>;
template class LuaStackOp<float>;
template class LuaStackOp<String>;

/* USERDATA */

#define LUA_UDATA_STACK_OP(type, metatable_name, dtor)                                      \
    template <>                                                                             \
    void LuaStackOp<type>::push(lua_State *L, const type &value)                            \
    {                                                                                       \
        type *udata = reinterpret_cast<type *>(lua_newuserdatadtor(L, sizeof(type), dtor)); \
        *udata = value;                                                                     \
                                                                                            \
        luaL_getmetatable(L, #metatable_name);                                              \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " #metatable_name);                         \
                                                                                            \
        lua_setmetatable(L, -2);                                                            \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    bool LuaStackOp<type>::is(lua_State *L, int index)                                      \
    {                                                                                       \
        if (lua_type(L, index) != LUA_TUSERDATA)                                            \
            return false;                                                                   \
                                                                                            \
        int abs_index = lua_absindex(L, index);                                             \
                                                                                            \
        luaL_getmetatable(L, #metatable_name);                                              \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " #metatable_name);                         \
                                                                                            \
        bool has_metatable = lua_getmetatable(L, abs_index);                                \
        if (!has_metatable || !lua_equal(L, -1, -2))                                        \
        {                                                                                   \
            lua_pop(L, has_metatable ? 2 : 1);                                              \
            return false;                                                                   \
        }                                                                                   \
                                                                                            \
        lua_pop(L, 2);                                                                      \
                                                                                            \
        return true;                                                                        \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    type *LuaStackOp<type>::get_ptr(lua_State *L, int index)                                \
    {                                                                                       \
        if (!LuaStackOp<type>::is(L, index))                                                \
            return nullptr;                                                                 \
                                                                                            \
        return reinterpret_cast<type *>(lua_touserdata(L, index));                          \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    type LuaStackOp<type>::get(lua_State *L, int index)                                     \
    {                                                                                       \
        type *udata = LuaStackOp<type>::get_ptr(L, index);                                  \
        if (!udata)                                                                         \
            return type();                                                                  \
                                                                                            \
        return *udata;                                                                      \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    type *LuaStackOp<type>::check_ptr(lua_State *L, int index)                              \
    {                                                                                       \
        return reinterpret_cast<type *>(luaL_checkudata(L, index, #metatable_name));        \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    type LuaStackOp<type>::check(lua_State *L, int index)                                   \
    {                                                                                       \
        return *LuaStackOp<type>::check_ptr(L, index);                                      \
    }

#define NO_DTOR [](void *) {}
#define DTOR(type)                                \
    [](void *udata)                               \
    {                                             \
        reinterpret_cast<type *>(udata)->~type(); \
    }
