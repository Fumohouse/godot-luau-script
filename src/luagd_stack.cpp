#include "luagd_stack.h"

#include <lua.h>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// Template specialization is weird!!!
// This way seems to work fine...

#define LUA_BASIC_STACK_OP(type, op_name)                                                         \
    template <>                                                                                   \
    void LuaStackOp<type>::push(lua_State *L, const type &value) { lua_push##op_name(L, value); } \
                                                                                                  \
    template <>                                                                                   \
    type LuaStackOp<type>::get(lua_State *L, int index) { return lua_to##op_name(L, index); }

LUA_BASIC_STACK_OP(bool, boolean);
LUA_BASIC_STACK_OP(int, integer);
LUA_BASIC_STACK_OP(float, number);

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
