#pragma once

#include <lua.h>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

template <typename T>
class LuaStackOp
{
public:
    static void push(lua_State *L, const T &value);
    static T get(lua_State *L, int index);
};

template class LuaStackOp<bool>;
template class LuaStackOp<int>;
template class LuaStackOp<float>;
template class LuaStackOp<String>;
