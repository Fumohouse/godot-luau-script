#pragma once

#include <catch_amalgamated.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

using namespace godot;

struct lua_State;

class LuauFixture
{
protected:
    lua_State *L;

public:
    LuauFixture();
    ~LuauFixture();
};

struct ExecOutput
{
    Error status;
    String error;
};

ExecOutput luaGD_exec(lua_State *L, const char *src);

#define ASSERT_EVAL_EQ(L, src, type, value)             \
    {                                                   \
        ExecOutput out = luaGD_exec(L, src);            \
                                                        \
        if (out.status != OK)                           \
            FAIL(out.error.utf8().get_data());          \
                                                        \
        CHECK(LuaStackOp<type>::check(L, -1) == value); \
        lua_pop(L, 1);                                  \
    }

#define ASSERT_EVAL_OK(L, src)                 \
    {                                          \
        int top = lua_gettop(L);               \
        ExecOutput out = luaGD_exec(L, src);   \
                                               \
        if (out.status != OK)                  \
            FAIL(out.error.utf8().get_data()); \
                                               \
        lua_settop(L, top);                    \
    }

#define ASSERT_EVAL_FAIL(L, src, err)        \
    {                                        \
        ExecOutput out = luaGD_exec(L, src); \
                                             \
        REQUIRE(out.status != OK);           \
        REQUIRE(out.error == err);           \
    }
