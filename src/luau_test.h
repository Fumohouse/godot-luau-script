#pragma once

#ifdef DEBUG_ENABLED
#include <lua.h>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/wrapped.hpp>

#include "luagd.h"

using namespace godot;

#define LUAU_TEST_STACK_OPS(type, name)   \
    void _push_##name(type value)         \
    {                                     \
        luaGD_push<type>(L, value);       \
    }                                     \
                                          \
    type _get_##name(int index)           \
    {                                     \
        return luaGD_get<type>(L, index); \
    }

// A test node for working with the Luau VM directly,
// instead of as a scripting language.

class LuauTest : public Node
{
    GDCLASS(LuauTest, Node);

private:
    lua_State *L;

protected:
    static void _bind_methods();

    LUAU_TEST_STACK_OPS(bool, boolean);
    LUAU_TEST_STACK_OPS(int, integer);
    LUAU_TEST_STACK_OPS(float, number);
    LUAU_TEST_STACK_OPS(String, string);

    void _set_top(int index);

public:
    LuauTest();
    ~LuauTest();
};
#endif // DEBUG_ENABLED
