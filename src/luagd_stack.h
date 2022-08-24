#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/global_constants.hpp>

using namespace godot;

template <typename T>
class LuaStackOp
{
public:
    static void push(lua_State *L, const T &value);

    static T get(lua_State *L, int index);
    static bool is(lua_State *L, int index);
    static T check(lua_State *L, int index);

    /* USERDATA */

    static T *alloc(lua_State *L);
    static T *get_ptr(lua_State *L, int index);
    static T *check_ptr(lua_State *L, int index);
};

template class LuaStackOp<bool>;
template class LuaStackOp<int>;
template class LuaStackOp<float>;
template class LuaStackOp<String>;

/* FOR BINDINGS */

template class LuaStackOp<double>;
template class LuaStackOp<int8_t>;
template class LuaStackOp<uint8_t>;
template class LuaStackOp<int16_t>;
template class LuaStackOp<uint16_t>;
template class LuaStackOp<uint32_t>;
template class LuaStackOp<int64_t>;
template class LuaStackOp<Object *>;

template class LuaStackOp<Error>;

/* USERDATA */

#define LUA_UDATA_STACK_OP(type, metatable_name, dtor)                                      \
    template <>                                                                             \
    type *LuaStackOp<type>::alloc(lua_State *L)                                             \
    {                                                                                       \
        type *udata = reinterpret_cast<type *>(lua_newuserdatadtor(L, sizeof(type), dtor)); \
                                                                                            \
        luaL_getmetatable(L, #metatable_name);                                              \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " #metatable_name);                         \
                                                                                            \
        lua_setmetatable(L, -2);                                                            \
                                                                                            \
        return udata;                                                                       \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    void LuaStackOp<type>::push(lua_State *L, const type &value)                            \
    {                                                                                       \
        type *udata = LuaStackOp<type>::alloc(L);                                           \
        *udata = value;                                                                     \
    }                                                                                       \
                                                                                            \
    template <>                                                                             \
    bool LuaStackOp<type>::is(lua_State *L, int index)                                      \
    {                                                                                       \
        if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))             \
            return false;                                                                   \
                                                                                            \
        luaL_getmetatable(L, #metatable_name);                                              \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " #metatable_name);                         \
                                                                                            \
        bool result = lua_equal(L, -1, -2);                                                 \
        lua_pop(L, 2);                                                                      \
                                                                                            \
        return result;                                                                      \
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

/* OBJECTS */

#define LUA_OBJECT_STACK_OP(type, metatable_name)                   \
    template <>                                                     \
    void LuaStackOp<type *>::push(lua_State *L, type *const &value) \
    {                                                               \
        LuaStackOp<Object *>::push(L, value);                       \
    }                                                               \
                                                                    \
    template <>                                                     \
    type *LuaStackOp<type *>::get(lua_State *L, int index)          \
    {                                                               \
        Object *obj = LuaStackOp<Object *>::get(L, index);          \
        if (obj == nullptr)                                         \
            return nullptr;                                         \
                                                                    \
        return Object::cast_to<type>(obj);                          \
    }                                                               \
                                                                    \
    template <>                                                     \
    bool LuaStackOp<type *>::is(lua_State *L, int index)            \
    {                                                               \
        return LuaStackOp<type *>::get(L, index) != nullptr;        \
    }                                                               \
                                                                    \
    template <>                                                     \
    type *LuaStackOp<type *>::check(lua_State *L, int index)        \
    {                                                               \
        type *ptr = LuaStackOp<type *>::get(L, index);              \
        if (ptr == nullptr)                                         \
            luaL_typeerrorL(L, index, #type);                       \
                                                                    \
        return ptr;                                                 \
    }
