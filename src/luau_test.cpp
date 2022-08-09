#ifdef DEBUG_ENABLED
#include "luau_test.h"

#include <lua.h>
#include <godot_cpp/variant/utility_functions.hpp>

#include "luagd.h"
#include "luagd_stack.h"

LuauTest::LuauTest()
{
    UtilityFunctions::print("LuauTest initializing...");

    L = luaGD_newstate();
}

LuauTest::~LuauTest()
{
    UtilityFunctions::print("LuauTest uninitializing...");

    lua_close(L);
}

void LuauTest::_set_top(int index)
{
    lua_settop(L, index);
}

#define LUAU_TEST_BIND_STACK_OPS(name)                                               \
    ClassDB::bind_method(D_METHOD("push_" #name, "value"), &LuauTest::_push_##name); \
    ClassDB::bind_method(D_METHOD("get_" #name, "index"), &LuauTest::_get_##name);

void LuauTest::_bind_methods()
{
    LUAU_TEST_BIND_STACK_OPS(boolean);
    LUAU_TEST_BIND_STACK_OPS(integer);
    LUAU_TEST_BIND_STACK_OPS(number);
    LUAU_TEST_BIND_STACK_OPS(string);

    ClassDB::bind_method(D_METHOD("set_top", "index"), &LuauTest::_set_top);
}
#endif // DEBUG_ENABLED
