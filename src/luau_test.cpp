#ifdef DEBUG_ENABLED
#include "luau_test.h"

#include <string>
#include <lua.h>
#include <Luau/Compiler.h>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include "luagd.h"
#include "luagd_builtins.h"

LuauTest::LuauTest()
{
    UtilityFunctions::print("LuauTest initializing...");

    L = luaGD_newstate();
    luaGD_openbuiltins(L);
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

bool LuauTest::_gc_step(int size)
{
    return lua_gc(L, LUA_GCSTEP, size);
}

Dictionary LuauTest::_exec(String source)
{
    Luau::CompileOptions opts;
    std::string bytecode = Luau::compile(std::string(source.utf8().get_data()), opts);

    Dictionary output;

    if (luau_load(L, "=exec", bytecode.data(), bytecode.size(), 0) == 0)
    {
        int status = lua_resume(L, nullptr, 0);

        if (status == LUA_OK)
        {
            output["status"] = OK;
            return output;
        }
        else if (status == LUA_YIELD)
        {
            output["status"] = FAILED;
            output["error"] = "Unexpected yield";
            return output;
        }

        String error = luaGD_get<String>(L, -1);
        lua_pop(L, 1);

        output["status"] = FAILED;
        output["error"] = error;
        return output;
    }

    output["status"] = FAILED;
    output["error"] = "Failed to load";
    return output;
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

    // lol
    LUAU_TEST_BIND_STACK_OPS(vector2);
    LUAU_TEST_BIND_STACK_OPS(vector2i);
    LUAU_TEST_BIND_STACK_OPS(rect2);
    LUAU_TEST_BIND_STACK_OPS(rect2i);
    LUAU_TEST_BIND_STACK_OPS(vector3);
    LUAU_TEST_BIND_STACK_OPS(vector3i);
    LUAU_TEST_BIND_STACK_OPS(transform2D);
    LUAU_TEST_BIND_STACK_OPS(vector4);
    LUAU_TEST_BIND_STACK_OPS(vector4i);
    LUAU_TEST_BIND_STACK_OPS(plane);
    LUAU_TEST_BIND_STACK_OPS(quaternion);
    LUAU_TEST_BIND_STACK_OPS(aabb);
    LUAU_TEST_BIND_STACK_OPS(basis);
    LUAU_TEST_BIND_STACK_OPS(transform3D);
    LUAU_TEST_BIND_STACK_OPS(projection);
    LUAU_TEST_BIND_STACK_OPS(color);
    LUAU_TEST_BIND_STACK_OPS(string_name);
    LUAU_TEST_BIND_STACK_OPS(node_path);
    LUAU_TEST_BIND_STACK_OPS(rid);
    LUAU_TEST_BIND_STACK_OPS(callable);
    LUAU_TEST_BIND_STACK_OPS(signal);
    LUAU_TEST_BIND_STACK_OPS(dictionary);
    LUAU_TEST_BIND_STACK_OPS(array);
    LUAU_TEST_BIND_STACK_OPS(packed_byte_array);
    LUAU_TEST_BIND_STACK_OPS(packed_int32_array);
    LUAU_TEST_BIND_STACK_OPS(packed_int64_array);
    LUAU_TEST_BIND_STACK_OPS(packed_float32_array);
    LUAU_TEST_BIND_STACK_OPS(packed_float64_array);
    LUAU_TEST_BIND_STACK_OPS(packed_string_array);
    LUAU_TEST_BIND_STACK_OPS(packed_vector2_array);
    LUAU_TEST_BIND_STACK_OPS(packed_vector3_array);
    LUAU_TEST_BIND_STACK_OPS(packed_color_aray);

    ClassDB::bind_method(D_METHOD("set_top", "index"), &LuauTest::_set_top);
    ClassDB::bind_method(D_METHOD("gc_step", "size"), &LuauTest::_gc_step);
    ClassDB::bind_method(D_METHOD("exec", "source"), &LuauTest::_exec);
}
#endif // DEBUG_ENABLED
