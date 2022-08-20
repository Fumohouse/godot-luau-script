#include "luagd_stack.h"

#include <string>
#include <lua.h>
#include <lualib.h>
#include <godot/gdnative_interface.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

using namespace godot;

// Template specialization is weird!!!
// This way seems to work fine...

/* BASIC TYPES */

#define LUA_BASIC_STACK_OP(type, op_name, is_name)                                                \
    template <>                                                                                   \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { lua_push##op_name(L, value); } \
                                                                                                  \
    template <>                                                                                   \
    type LuaStackOp<type>::get(lua_State *L, int index) { return lua_to##op_name(L, index); }     \
                                                                                                  \
    template <>                                                                                   \
    bool LuaStackOp<type>::is(lua_State *L, int index) { return lua_is##is_name(L, index); }      \
                                                                                                  \
    template <>                                                                                   \
    type LuaStackOp<type>::check(lua_State *L, int index) { return luaL_check##op_name(L, index); }

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

/* STRING */

template <>
void LuaStackOp<String>::push(lua_State *L, const String &value)
{
    lua_pushstring(L, value.utf8().get_data());
}

template <>
String LuaStackOp<String>::get(lua_State *L, int index)
{
    return String::utf8(lua_tostring(L, index));
}

template <>
bool LuaStackOp<String>::is(lua_State *L, int index)
{
    return lua_isstring(L, index);
}

template <>
String LuaStackOp<String>::check(lua_State *L, int index)
{
    return String::utf8(luaL_checkstring(L, index));
}

/* OBJECTS */

static void luaGD_object_init(Object *ptr)
{
    RefCounted *rc = Object::cast_to<RefCounted>(ptr);
    if (rc != nullptr)
        rc->reference();
}

static void luaGD_object_dtor(void *ptr)
{
    Object *instance = ObjectDB::get_instance(
        *reinterpret_cast<GDObjectInstanceID *>(ptr));

    RefCounted *rc = Object::cast_to<RefCounted>(instance);
    if (rc != nullptr)
        rc->unreference();
}

template <>
void LuaStackOp<Object *>::push(lua_State *L, Object *const &value)
{
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
Object *LuaStackOp<Object *>::get(lua_State *L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index))
        return nullptr;

    lua_getfield(L, -1, "__isgdobj");
    if (!lua_isboolean(L, -1))
    {
        lua_pop(L, 2);
        return nullptr;
    }

    bool is_obj = lua_toboolean(L, -1);
    lua_pop(L, 2);

    if (!is_obj)
        return nullptr;

    GDObjectInstanceID *udata = reinterpret_cast<GDObjectInstanceID *>(lua_touserdata(L, index));
    Object *obj_ptr = ObjectDB::get_instance(*udata);

    return obj_ptr;
}

template <>
bool LuaStackOp<Object *>::is(lua_State *L, int index)
{
    return LuaStackOp<Object *>::get(L, index) != nullptr;
}

template <>
Object *LuaStackOp<Object *>::check(lua_State *L, int index)
{
    Object *ptr = LuaStackOp<Object *>::get(L, index);
    if (ptr == nullptr)
        luaL_typeerrorL(L, index, "Object");

    return ptr;
}
