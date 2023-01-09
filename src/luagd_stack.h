#pragma once

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {
class Object;
}

using namespace godot;

template <typename T>
class LuaStackOp {
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

template <>
class LuaStackOp<Array> {
public:
    static void push(lua_State *L, const Array &value);

    static Array get(lua_State *L, int index, Variant::Type type = Variant::NIL, const String &class_name = "");
    static bool is(lua_State *L, int index, Variant::Type type = Variant::NIL, const String &class_name = "");
    static Array check(lua_State *L, int index, Variant::Type type = Variant::NIL, const String &class_name = "");

    /* USERDATA */

    static Array *alloc(lua_State *L);
    static Array *get_ptr(lua_State *L, int index);
    static Array *check_ptr(lua_State *L, int index);
};

/* USERDATA */

bool luaGD_metatables_match(lua_State *L, int index, const char *metatable_name);

#define LUA_UDATA_ALLOC(type, metatable_name, dtor)                                         \
    type *LuaStackOp<type>::alloc(lua_State *L) {                                           \
        type *udata = reinterpret_cast<type *>(lua_newuserdatadtor(L, sizeof(type), dtor)); \
                                                                                            \
        luaL_getmetatable(L, metatable_name);                                               \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " metatable_name);                          \
                                                                                            \
        lua_setmetatable(L, -2);                                                            \
                                                                                            \
        return udata;                                                                       \
    }

#define LUA_UDATA_PUSH(type)                                       \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { \
        type *udata = LuaStackOp<type>::alloc(L);                  \
        new (udata) type();                                        \
        *udata = value;                                            \
    }

#define LUA_UDATA_GET_PTR(type, metatable_name)                    \
    type *LuaStackOp<type>::get_ptr(lua_State *L, int index) {     \
        if (!luaGD_metatables_match(L, index, metatable_name))     \
            return nullptr;                                        \
                                                                   \
        return reinterpret_cast<type *>(lua_touserdata(L, index)); \
    }

#define LUA_UDATA_CHECK_PTR(type, metatable_name)                                   \
    type *LuaStackOp<type>::check_ptr(lua_State *L, int index) {                    \
        return reinterpret_cast<type *>(luaL_checkudata(L, index, metatable_name)); \
    }

#define LUA_UDATA_STACK_OP(type, metatable_name, dtor)           \
    template <>                                                  \
    LUA_UDATA_ALLOC(type, metatable_name, dtor)                  \
                                                                 \
    template <>                                                  \
    LUA_UDATA_PUSH(type)                                         \
                                                                 \
    template <>                                                  \
    bool LuaStackOp<type>::is(lua_State *L, int index) {         \
        return luaGD_metatables_match(L, index, metatable_name); \
    }                                                            \
                                                                 \
    template <>                                                  \
    LUA_UDATA_GET_PTR(type, metatable_name)                      \
                                                                 \
    template <>                                                  \
    type LuaStackOp<type>::get(lua_State *L, int index) {        \
        type *udata = LuaStackOp<type>::get_ptr(L, index);       \
        if (!udata)                                              \
            return type();                                       \
                                                                 \
        return *udata;                                           \
    }                                                            \
                                                                 \
    template <>                                                  \
    LUA_UDATA_CHECK_PTR(type, metatable_name)                    \
                                                                 \
    template <>                                                  \
    type LuaStackOp<type>::check(lua_State *L, int index) {      \
        return *LuaStackOp<type>::check_ptr(L, index);           \
    }

#define NO_DTOR [](void *) {}
#define DTOR(type)                                \
    [](void *udata) {                             \
        reinterpret_cast<type *>(udata)->~type(); \
    }

/* ARRAY */

bool luaGD_isarray(lua_State *L, int index, const char *metatable_name, Variant::Type type, const String &class_name);

template <typename TArray>
struct ArraySetter {
    typedef void (*ArraySet)(TArray &array, int index, Variant elem);
};

template <typename TArray>
TArray luaGD_getarray(lua_State *L, int index, const char *metatable_name, Variant::Type type, const String &class_name, typename ArraySetter<TArray>::ArraySet setter) {
    if (luaGD_metatables_match(L, index, metatable_name))
        return *LuaStackOp<TArray>::get_ptr(L, index);

    if (!lua_istable(L, index))
        return TArray();

    index = lua_absindex(L, index);

    int len = lua_objlen(L, index);

    TArray arr;
    arr.resize(len);

    for (int i = 0; i < len; i++) {
        lua_pushinteger(L, i + 1);
        lua_gettable(L, index);

        Variant elem = LuaStackOp<Variant>::get(L, -1);
        lua_pop(L, 1);

        if (type != Variant::NIL &&
                (elem.get_type() != type ||
                        (type == Variant::OBJECT && !elem.operator Object *()->is_class(class_name)))) {
            return TArray();
        }

        setter(arr, i, elem);
    }

    return arr;
}

template <typename TArray>
TArray luaGD_checkarray(lua_State *L, int index, const char *metatable_name, Variant::Type type, const String &class_name, typename ArraySetter<TArray>::ArraySet setter) {
    if (luaGD_metatables_match(L, index, metatable_name))
        return *LuaStackOp<TArray>::get_ptr(L, index);

    if (!lua_istable(L, index))
        return TArray();

    index = lua_absindex(L, index);

    int len = lua_objlen(L, index);

    TArray arr;
    arr.resize(len);

    for (int i = 0; i < len; i++) {
        lua_pushinteger(L, i + 1);
        lua_gettable(L, index);

        Variant elem = LuaStackOp<Variant>::get(L, -1);
        lua_pop(L, 1);

        Variant::Type elem_type = elem.get_type();

        Object *obj = elem;

        if (type != Variant::NIL &&
                (elem_type != type ||
                        (type == Variant::OBJECT && !obj->is_class(class_name)))) {
            String elem_type_name, expected_type_name;

            if (type == Variant::OBJECT) {
                elem_type_name = obj->get_class();
                expected_type_name = class_name;
            } else {
                elem_type_name = Variant::get_type_name(elem_type);
                expected_type_name = Variant::get_type_name(type);
            }

            luaL_error(L, "expected type %s for typed array element, got %s (index %d)",
                    expected_type_name.utf8().get_data(),
                    elem_type_name.utf8().get_data(),
                    i);
        }

        setter(arr, i, elem);
    }

    return arr;
}

#define LUA_ARRAY_STACK_OP(type, variant_type, elem_type, metatable_name)                      \
    static void type##_set(type &array, int index, Variant elem) {                             \
        array[index] = elem.operator elem_type();                                              \
    }                                                                                          \
                                                                                               \
    template <>                                                                                \
    LUA_UDATA_ALLOC(type, metatable_name, DTOR(type))                                          \
                                                                                               \
    template <>                                                                                \
    LUA_UDATA_PUSH(type)                                                                       \
                                                                                               \
    template <>                                                                                \
    bool LuaStackOp<type>::is(lua_State *L, int index) {                                       \
        return luaGD_isarray(L, index, metatable_name, variant_type, "");                      \
    }                                                                                          \
                                                                                               \
    template <>                                                                                \
    LUA_UDATA_GET_PTR(type, metatable_name)                                                    \
                                                                                               \
    template <>                                                                                \
    type LuaStackOp<type>::get(lua_State *L, int index) {                                      \
        return luaGD_getarray<type>(L, index, metatable_name, variant_type, "", type##_set);   \
    }                                                                                          \
                                                                                               \
    template <>                                                                                \
    LUA_UDATA_CHECK_PTR(type, metatable_name)                                                  \
                                                                                               \
    template <>                                                                                \
    type LuaStackOp<type>::check(lua_State *L, int index) {                                    \
        return luaGD_checkarray<type>(L, index, metatable_name, variant_type, "", type##_set); \
    }

/* OBJECTS */

#define LUA_OBJECT_STACK_OP(type)                                     \
    template <>                                                       \
    void LuaStackOp<type *>::push(lua_State *L, type *const &value) { \
        LuaStackOp<Object *>::push(L, value);                         \
    }                                                                 \
                                                                      \
    template <>                                                       \
    type *LuaStackOp<type *>::get(lua_State *L, int index) {          \
        Object *obj = LuaStackOp<Object *>::get(L, index);            \
        if (obj == nullptr)                                           \
            return nullptr;                                           \
                                                                      \
        return Object::cast_to<type>(obj);                            \
    }                                                                 \
                                                                      \
    template <>                                                       \
    bool LuaStackOp<type *>::is(lua_State *L, int index) {            \
        Object *obj = LuaStackOp<Object *>::get(L, index);            \
        if (obj == nullptr)                                           \
            return false;                                             \
                                                                      \
        return obj->is_class(#type);                                  \
    }                                                                 \
                                                                      \
    template <>                                                       \
    type *LuaStackOp<type *>::check(lua_State *L, int index) {        \
        if (!LuaStackOp<type *>::is(L, index))                        \
            luaL_typeerrorL(L, index, #type);                         \
                                                                      \
        return LuaStackOp<type *>::get(L, index);                     \
    }
