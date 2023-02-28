#include "luagd_stack.h"
#include "luagd_variant.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <cmath>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_bindings.h"
#include "luau_script.h"
#include "utils.h"

using namespace godot;

bool luaGD_metatables_match(lua_State *L, int index, const char *metatable_name) {
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))
        return false;

    luaL_getmetatable(L, metatable_name);
    if (lua_isnil(L, -1))
        luaL_error(L, "Metatable not found: %s", metatable_name);

    bool result = lua_equal(L, -1, -2);
    lua_pop(L, 2);

    return result;
}

/* BASIC TYPES */

#define BASIC_STACK_OP_IMPL(type, op_name, is_name)                                                              \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { lua_push##op_name(L, value); }                \
    type LuaStackOp<type>::get(lua_State *L, int index) { return static_cast<type>(lua_to##op_name(L, index)); } \
    bool LuaStackOp<type>::is(lua_State *L, int index) { return lua_is##is_name(L, index); }                     \
    type LuaStackOp<type>::check(lua_State *L, int index) { return static_cast<type>(luaL_check##op_name(L, index)); }

BASIC_STACK_OP_IMPL(bool, boolean, boolean);
BASIC_STACK_OP_IMPL(int, integer, number);
BASIC_STACK_OP_IMPL(float, number, number);

// this is only a little bit shady. don't worry about it
BASIC_STACK_OP_IMPL(double, number, number);
BASIC_STACK_OP_IMPL(int8_t, number, number);
BASIC_STACK_OP_IMPL(uint8_t, unsigned, number);
BASIC_STACK_OP_IMPL(int16_t, number, number);
BASIC_STACK_OP_IMPL(uint16_t, unsigned, number);
BASIC_STACK_OP_IMPL(uint32_t, unsigned, number);
BASIC_STACK_OP_IMPL(int64_t, number, number);

/* STRING */

void LuaStackOp<String>::push(lua_State *L, const String &value) {
    lua_pushstring(L, value.utf8().get_data());
}

String LuaStackOp<String>::get(lua_State *L, int index) {
    return String::utf8(lua_tostring(L, index));
}

bool LuaStackOp<String>::is(lua_State *L, int index) {
    return lua_isstring(L, index);
}

String LuaStackOp<String>::check(lua_State *L, int index) {
    return String::utf8(luaL_checkstring(L, index));
}

/* OBJECTS */

static void luaGD_object_init(Object *ptr) {
    RefCounted *rc = Object::cast_to<RefCounted>(ptr);
    if (rc)
        rc->init_ref();
}

static void luaGD_object_dtor(void *ptr) {
    GDObjectInstanceID id = *reinterpret_cast<GDObjectInstanceID *>(ptr);
    if (id == 0)
        return;

    Object *instance = ObjectDB::get_instance(id);

    RefCounted *rc = Object::cast_to<RefCounted>(instance);
    if (rc)
        rc->unreference();
}

void LuaStackOp<Object *>::push(lua_State *L, Object *value) {
    if (value) {
        GDObjectInstanceID *udata =
                reinterpret_cast<GDObjectInstanceID *>(lua_newuserdatadtor(L, sizeof(GDObjectInstanceID), luaGD_object_dtor));

        // Must search parent classes because some classes used in Godot are not registered,
        // e.g. GodotPhysicsDirectSpaceState3D -> PhysicsDirectSpaceState3D

        bool mt_found = false;
        StringName curr_class = value->get_class();

        while (!curr_class.is_empty()) {
            String metatable_name = "Godot.Object." + curr_class;
            if (!LuauScriptInstance::from_object(value)) {
                metatable_name = metatable_name + ".Namecall";
            }

            luaL_getmetatable(L, metatable_name.utf8().get_data());
            if (!lua_isnil(L, -1)) {
                mt_found = true;
                break;
            }

            lua_pop(L, 1); // nil

            curr_class = Utils::get_parent_class(curr_class);
        }

        luaGD_object_init(value);
        *udata = value->get_instance_id();

        // Shouldn't be possible
        if (!lua_istable(L, -1))
            luaL_error(L, "Metatable not found for class %s", value->get_class().utf8().get_data());

        lua_setmetatable(L, -2);
    } else {
        lua_pushnil(L);
    }
}

bool LuaStackOp<Object *>::is(lua_State *L, int index) {
    if (lua_isnil(L, index))
        return true;

    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))
        return false;

    lua_getfield(L, -1, "__gdclass");

    bool is_obj = lua_isnumber(L, -1);
    lua_pop(L, 2); // value, metatable

    return is_obj;
}

GDObjectInstanceID *LuaStackOp<Object *>::get_ptr(lua_State *L, int index) {
    return reinterpret_cast<GDObjectInstanceID *>(lua_touserdata(L, index));
}

Object *LuaStackOp<Object *>::get(lua_State *L, int index) {
    if (!LuaStackOp<Object *>::is(L, index))
        return nullptr;

    GDObjectInstanceID *udata = LuaStackOp<Object *>::get_ptr(L, index);
    if (!udata || *udata == 0)
        return nullptr;

    return ObjectDB::get_instance(*udata);
}

Object *LuaStackOp<Object *>::check(lua_State *L, int index) {
    if (!LuaStackOp<Object *>::is(L, index))
        luaL_typeerrorL(L, index, "Object");

    return LuaStackOp<Object *>::get(L, index);
}

/* VARIANT */

void LuaStackOp<Variant>::push(lua_State *L, const Variant &value) {
    LuauVariant lv;

    lv.initialize(GDExtensionVariantType(value.get_type()));
    lv.assign_variant(value);
    lv.lua_push(L);
}

Variant LuaStackOp<Variant>::get(lua_State *L, int index) {
    int type = LuaStackOp<Variant>::get_type(L, index);
    if (type == -1)
        return Variant();

    LuauVariant lv;
    lv.lua_check(L, index, GDExtensionVariantType(type));

    return lv.to_variant();
}

bool LuaStackOp<Variant>::is(lua_State *L, int index) {
    return LuaStackOp<Variant>::get_type(L, index) != -1;
}

int LuaStackOp<Variant>::get_type(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            return GDEXTENSION_VARIANT_TYPE_NIL;

        case LUA_TBOOLEAN:
            return GDEXTENSION_VARIANT_TYPE_BOOL;

        case LUA_TNUMBER: {
            // Somewhat frail...
            double value = lua_tonumber(L, index);
            double int_part;

            if (std::modf(value, &int_part) == 0.0)
                return GDEXTENSION_VARIANT_TYPE_INT;
            else
                return GDEXTENSION_VARIANT_TYPE_FLOAT;
        }

        case LUA_TSTRING:
            return GDEXTENSION_VARIANT_TYPE_STRING;

        case LUA_TUSERDATA:
            // Pass through to below with metatable on stack
            if (!lua_getmetatable(L, index))
                return -1;

            break;

        default:
            return -1;
    }

    lua_getfield(L, -1, MT_VARIANT_TYPE);

    if (lua_isnil(L, -1))
        return -1;

    int type = lua_tonumber(L, -1);
    lua_pop(L, 2); // value, metatable

    return type;
}

Variant LuaStackOp<Variant>::check(lua_State *L, int index) {
    int type = LuaStackOp<Variant>::get_type(L, index);
    if (type == -1)
        luaL_typeerrorL(L, index, "Variant");

    LuauVariant lv;
    lv.lua_check(L, index, GDExtensionVariantType(type));

    return lv.to_variant();
}

/* STRING COERCION */

#define STR_STACK_OP_IMPL(type)                                                                   \
    UDATA_ALLOC(type, BUILTIN_MT_NAME(type), DTOR(type))                                          \
                                                                                                  \
    void LuaStackOp<type>::push(lua_State *L, const type &value, bool force_type) {               \
        if (force_type) {                                                                         \
            type *udata = LuaStackOp<type>::alloc(L);                                             \
            *udata = value;                                                                       \
        } else {                                                                                  \
            LuaStackOp<String>::push(L, String(value));                                           \
        }                                                                                         \
    }                                                                                             \
                                                                                                  \
    bool LuaStackOp<type>::is(lua_State *L, int index) {                                          \
        return lua_isstring(L, index) || luaGD_metatables_match(L, index, BUILTIN_MT_NAME(type)); \
    }                                                                                             \
                                                                                                  \
    UDATA_GET_PTR(type, BUILTIN_MT_NAME(type))                                                    \
                                                                                                  \
    type LuaStackOp<type>::get(lua_State *L, int index) {                                         \
        if (luaGD_metatables_match(L, index, BUILTIN_MT_NAME(type)))                              \
            return *LuaStackOp<type>::get_ptr(L, index);                                          \
                                                                                                  \
        return type(lua_tostring(L, index));                                                      \
    }                                                                                             \
                                                                                                  \
    UDATA_CHECK_PTR(type, BUILTIN_MT_NAME(type))                                                  \
                                                                                                  \
    type LuaStackOp<type>::check(lua_State *L, int index) {                                       \
        if (lua_isstring(L, index))                                                               \
            return type(lua_tostring(L, index));                                                  \
                                                                                                  \
        if (luaGD_metatables_match(L, index, BUILTIN_MT_NAME(type)))                              \
            return *LuaStackOp<type>::get_ptr(L, index);                                          \
                                                                                                  \
        luaL_typeerrorL(L, index, #type " or string");                                            \
    }

STR_STACK_OP_IMPL(StringName)
STR_STACK_OP_IMPL(NodePath)

/* ARRAY */

bool luaGD_isarray(lua_State *L, int index, const char *metatable_name, Variant::Type type, const String &class_name) {
    if (luaGD_metatables_match(L, index, metatable_name))
        return true;

    if (!lua_istable(L, index))
        return false;

    if (type == Variant::NIL)
        return true;

    index = lua_absindex(L, index);

    int len = lua_objlen(L, index);
    for (int i = 1; i <= len; i++) {
        lua_pushinteger(L, i);
        lua_gettable(L, index);

        if (!LuauVariant::lua_is(L, -1, (GDExtensionVariantType)type, class_name))
            return false;

        lua_pop(L, 1);
    }

    return true;
}

#define ARRAY_METATABLE_NAME "Godot.Builtin.Array"

UDATA_PUSH(Array);

static void array_set(Array &array, int index, Variant elem) {
    array[index] = elem;
}

Array LuaStackOp<Array>::get(lua_State *L, int index) {
    return luaGD_getarray<Array>(L, index, ARRAY_METATABLE_NAME, Variant::NIL, "", array_set);
}

bool LuaStackOp<Array>::is(lua_State *L, int index) {
    return luaGD_isarray(L, index, ARRAY_METATABLE_NAME, Variant::NIL, "");
}

Array LuaStackOp<Array>::check(lua_State *L, int index) {
    return luaGD_checkarray<Array>(L, index, ARRAY_METATABLE_NAME, Variant::NIL, "", array_set);
}

UDATA_ALLOC(Array, ARRAY_METATABLE_NAME, DTOR(Array))
UDATA_GET_PTR(Array, ARRAY_METATABLE_NAME)
UDATA_CHECK_PTR(Array, ARRAY_METATABLE_NAME)
