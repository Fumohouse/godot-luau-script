#pragma once

#include <gdextension_interface.h>
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
struct LuaStackOp {};

#define STACK_OP_DEF_BASE(type, push_type)               \
    template <>                                          \
    struct LuaStackOp<type> {                            \
        static void push(lua_State *L, push_type value); \
                                                         \
        static type get(lua_State *L, int index);        \
        static bool is(lua_State *L, int index);         \
        static type check(lua_State *L, int index);      \
    };

#define STACK_OP_DEF(type) STACK_OP_DEF_BASE(type, const type &)

#define STACK_OP_PTR_DEF(type)                             \
    template <>                                            \
    struct LuaStackOp<type> {                              \
        static void push(lua_State *L, const type &value); \
                                                           \
        static type get(lua_State *L, int index);          \
        static bool is(lua_State *L, int index);           \
        static type check(lua_State *L, int index);        \
                                                           \
        /* USERDATA */                                     \
                                                           \
        static type *alloc(lua_State *L);                  \
        static type *get_ptr(lua_State *L, int index);     \
        static type *check_ptr(lua_State *L, int index);   \
    };

#define STACK_OP_STR_DEF(type)                                                      \
    template <>                                                                     \
    struct LuaStackOp<type> {                                                       \
        static void push(lua_State *L, const type &value, bool force_type = false); \
                                                                                    \
        static type get(lua_State *L, int index);                                   \
        static bool is(lua_State *L, int index);                                    \
        static type check(lua_State *L, int index);                                 \
                                                                                    \
        /* USERDATA */                                                              \
                                                                                    \
        static type *alloc(lua_State *L);                                           \
        static type *get_ptr(lua_State *L, int index);                              \
        static type *check_ptr(lua_State *L, int index);                            \
    };

STACK_OP_DEF(bool)
STACK_OP_DEF(int)
STACK_OP_DEF(float)
STACK_OP_DEF(String)

STACK_OP_DEF(double)
STACK_OP_DEF(int8_t)
STACK_OP_DEF(uint8_t)
STACK_OP_DEF(int16_t)
STACK_OP_DEF(uint16_t)
STACK_OP_DEF(uint32_t)
STACK_OP_DEF(int64_t)

template <>
struct LuaStackOp<Object *> {
    static void push(lua_State *L, Object *value);

    static Object *get(lua_State *L, int index);
    static bool is(lua_State *L, int index);
    static Object *check(lua_State *L, int index);

    static GDObjectInstanceID *get_ptr(lua_State *L, int index);
};

// Defined early to avoid specialization before declaraction.
// Implementation is generated.
STACK_OP_DEF(Variant)

STACK_OP_STR_DEF(StringName)
STACK_OP_STR_DEF(NodePath)

STACK_OP_PTR_DEF(Array)

/* USERDATA */

bool luaGD_metatables_match(lua_State *L, int index, const char *metatable_name);

#define UDATA_ALLOC(type, metatable_name, dtor)                                             \
    type *LuaStackOp<type>::alloc(lua_State *L) {                                           \
        type *udata = reinterpret_cast<type *>(lua_newuserdatadtor(L, sizeof(type), dtor)); \
        new (udata) type();                                                                 \
                                                                                            \
        luaL_getmetatable(L, metatable_name);                                               \
        if (lua_isnil(L, -1))                                                               \
            luaL_error(L, "Metatable not found: " metatable_name);                          \
                                                                                            \
        lua_setmetatable(L, -2);                                                            \
                                                                                            \
        return udata;                                                                       \
    }

#define UDATA_PUSH(type)                                           \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { \
        type *udata = LuaStackOp<type>::alloc(L);                  \
        *udata = value;                                            \
    }

#define UDATA_GET_PTR(type, metatable_name)                        \
    type *LuaStackOp<type>::get_ptr(lua_State *L, int index) {     \
        if (!luaGD_metatables_match(L, index, metatable_name))     \
            return nullptr;                                        \
                                                                   \
        return reinterpret_cast<type *>(lua_touserdata(L, index)); \
    }

#define UDATA_CHECK_PTR(type, metatable_name)                                       \
    type *LuaStackOp<type>::check_ptr(lua_State *L, int index) {                    \
        return reinterpret_cast<type *>(luaL_checkudata(L, index, metatable_name)); \
    }

#define UDATA_STACK_OP_IMPL(type, metatable_name, dtor)          \
    UDATA_ALLOC(type, metatable_name, dtor)                      \
    UDATA_PUSH(type)                                             \
                                                                 \
    bool LuaStackOp<type>::is(lua_State *L, int index) {         \
        return luaGD_metatables_match(L, index, metatable_name); \
    }                                                            \
                                                                 \
    UDATA_GET_PTR(type, metatable_name)                          \
                                                                 \
    type LuaStackOp<type>::get(lua_State *L, int index) {        \
        type *udata = LuaStackOp<type>::get_ptr(L, index);       \
        if (!udata)                                              \
            return type();                                       \
                                                                 \
        return *udata;                                           \
    }                                                            \
                                                                 \
    UDATA_CHECK_PTR(type, metatable_name)                        \
                                                                 \
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

#define ARRAY_STACK_OP_IMPL(type, variant_type, elem_type, metatable_name)                     \
    static void type##_set(type &array, int index, Variant elem) {                             \
        array[index] = elem.operator elem_type();                                              \
    }                                                                                          \
                                                                                               \
    UDATA_ALLOC(type, metatable_name, DTOR(type))                                              \
    UDATA_PUSH(type)                                                                           \
                                                                                               \
    bool LuaStackOp<type>::is(lua_State *L, int index) {                                       \
        return luaGD_isarray(L, index, metatable_name, variant_type, "");                      \
    }                                                                                          \
                                                                                               \
    UDATA_GET_PTR(type, metatable_name)                                                        \
                                                                                               \
    type LuaStackOp<type>::get(lua_State *L, int index) {                                      \
        return luaGD_getarray<type>(L, index, metatable_name, variant_type, "", type##_set);   \
    }                                                                                          \
                                                                                               \
    UDATA_CHECK_PTR(type, metatable_name)                                                      \
                                                                                               \
    type LuaStackOp<type>::check(lua_State *L, int index) {                                    \
        return luaGD_checkarray<type>(L, index, metatable_name, variant_type, "", type##_set); \
    }

/* POINTER */

#define PTR_OP_DEF(type) STACK_OP_DEF_BASE(type *, type *)

#define PTR_STACK_OP_IMPL(type, metatable_name)                                       \
    void LuaStackOp<type *>::push(lua_State *L, type *value) {                        \
        type **udata = reinterpret_cast<type **>(lua_newuserdata(L, sizeof(void *))); \
                                                                                      \
        luaL_getmetatable(L, metatable_name);                                         \
        if (lua_isnil(L, -1))                                                         \
            luaL_error(L, "Metatable not found: " metatable_name);                    \
                                                                                      \
        lua_setmetatable(L, -2);                                                      \
                                                                                      \
        *udata = value;                                                               \
    }                                                                                 \
                                                                                      \
    type *LuaStackOp<type *>::get(lua_State *L, int index) {                          \
        return *reinterpret_cast<type **>(lua_touserdata(L, index));                  \
    }                                                                                 \
                                                                                      \
    bool LuaStackOp<type *>::is(lua_State *L, int index) {                            \
        return luaGD_metatables_match(L, index, metatable_name);                      \
    }                                                                                 \
                                                                                      \
    type *LuaStackOp<type *>::check(lua_State *L, int index) {                        \
        return *reinterpret_cast<type **>(luaL_checkudata(L, index, metatable_name)); \
    }
