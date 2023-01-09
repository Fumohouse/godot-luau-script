#include "luagd_stack.h"
#include "luagd_variant.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>

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

// Template specialization is weird!!!
// This way seems to work fine...

/* BASIC TYPES */

#define LUA_BASIC_STACK_OP(type, op_name, is_name)                                                               \
    template <>                                                                                                  \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { lua_push##op_name(L, value); }                \
                                                                                                                 \
    template <>                                                                                                  \
    type LuaStackOp<type>::get(lua_State *L, int index) { return static_cast<type>(lua_to##op_name(L, index)); } \
                                                                                                                 \
    template <>                                                                                                  \
    bool LuaStackOp<type>::is(lua_State *L, int index) { return lua_is##is_name(L, index); }                     \
                                                                                                                 \
    template <>                                                                                                  \
    type LuaStackOp<type>::check(lua_State *L, int index) { return static_cast<type>(luaL_check##op_name(L, index)); }

LUA_BASIC_STACK_OP(bool, boolean, boolean);
LUA_BASIC_STACK_OP(int, integer, number);
LUA_BASIC_STACK_OP(float, number, number);

/* FOR BINDINGS */

// this is only a little bit shady. don't worry about it
LUA_BASIC_STACK_OP(double, number, number);
LUA_BASIC_STACK_OP(int8_t, number, number);
LUA_BASIC_STACK_OP(uint8_t, unsigned, number);
LUA_BASIC_STACK_OP(int16_t, number, number);
LUA_BASIC_STACK_OP(uint16_t, unsigned, number);
LUA_BASIC_STACK_OP(uint32_t, unsigned, number);
LUA_BASIC_STACK_OP(int64_t, number, number);
LUA_BASIC_STACK_OP(Error, integer, number);

/* STRING */

template <>
void LuaStackOp<String>::push(lua_State *L, const String &value) {
    lua_pushstring(L, value.utf8().get_data());
}

template <>
String LuaStackOp<String>::get(lua_State *L, int index) {
    return String::utf8(lua_tostring(L, index));
}

template <>
bool LuaStackOp<String>::is(lua_State *L, int index) {
    return lua_isstring(L, index);
}

template <>
String LuaStackOp<String>::check(lua_State *L, int index) {
    return String::utf8(luaL_checkstring(L, index));
}

/* OBJECTS */

static void luaGD_object_init(Object *ptr) {
    RefCounted *rc = Object::cast_to<RefCounted>(ptr);
    if (rc != nullptr)
        rc->init_ref();
}

static void luaGD_object_dtor(void *ptr) {
    Object *instance = ObjectDB::get_instance(
            *reinterpret_cast<GDObjectInstanceID *>(ptr));

    RefCounted *rc = Object::cast_to<RefCounted>(instance);
    if (rc != nullptr)
        rc->unreference();
}

template <>
void LuaStackOp<Object *>::push(lua_State *L, Object *const &value) {
    GDObjectInstanceID *udata =
            reinterpret_cast<GDObjectInstanceID *>(lua_newuserdatadtor(L, sizeof(GDObjectInstanceID), luaGD_object_dtor));

    luaGD_object_init(value);

    *udata = value->get_instance_id();

    String metatable_name = "Godot.Object." + value->get_class();
    const char *metatable_name_ptr = metatable_name.utf8().get_data();

    luaL_getmetatable(L, metatable_name_ptr);
    if (lua_isnil(L, -1))
        luaL_error(L, "Metatable not found: %s", metatable_name_ptr);

    lua_setmetatable(L, -2);
}

template <>
bool LuaStackOp<Object *>::is(lua_State *L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))
        return false;

    lua_getfield(L, -1, "__isgdobj");
    if (!lua_isboolean(L, -1)) {
        lua_pop(L, 2);
        return false;
    }

    bool is_obj = lua_toboolean(L, -1);
    lua_pop(L, 2);

    return is_obj;
}

template <>
Object *LuaStackOp<Object *>::get(lua_State *L, int index) {
    if (!LuaStackOp<Object *>::is(L, index))
        return nullptr;

    GDObjectInstanceID *udata = reinterpret_cast<GDObjectInstanceID *>(lua_touserdata(L, index));
    Object *obj_ptr = ObjectDB::get_instance(*udata);

    return obj_ptr;
}

template <>
Object *LuaStackOp<Object *>::check(lua_State *L, int index) {
    if (!LuaStackOp<Object *>::is(L, index))
        luaL_typeerrorL(L, index, "Object");

    return LuaStackOp<Object *>::get(L, index);
}

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

LUA_UDATA_PUSH(Array);

static void array_set(Array &array, int index, Variant elem) {
    array[index] = elem;
}

Array LuaStackOp<Array>::get(lua_State *L, int index, Variant::Type type, const String &class_name) {
    return luaGD_getarray<Array>(L, index, ARRAY_METATABLE_NAME, type, class_name, array_set);
}

bool LuaStackOp<Array>::is(lua_State *L, int index, Variant::Type type, const String &class_name) {
    return luaGD_isarray(L, index, ARRAY_METATABLE_NAME, type, class_name);
}

Array LuaStackOp<Array>::check(lua_State *L, int index, Variant::Type type, const String &class_name) {
    return luaGD_checkarray<Array>(L, index, ARRAY_METATABLE_NAME, type, class_name, array_set);
}

LUA_UDATA_ALLOC(Array, ARRAY_METATABLE_NAME, DTOR(Array))
LUA_UDATA_GET_PTR(Array, ARRAY_METATABLE_NAME)
LUA_UDATA_CHECK_PTR(Array, ARRAY_METATABLE_NAME)
