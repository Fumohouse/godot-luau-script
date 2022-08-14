#include "luagd_stack.h"

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/builtin_types.hpp>

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
