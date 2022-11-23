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

#define EVAL_THEN(L, src, expr)                \
    {                                          \
        int top = lua_gettop(L);               \
        ExecOutput out = luaGD_exec(L, src);   \
                                               \
        if (out.status != OK)                  \
            FAIL(out.error.utf8().get_data()); \
                                               \
        expr;                                  \
                                               \
        lua_settop(L, top);                    \
    }

#define ASSERT_EVAL_EQ(L, src, type, value)             \
    EVAL_THEN(L, src, {                                 \
        CHECK(LuaStackOp<type>::check(L, -1) == value); \
    });

#define ASSERT_EVAL_OK(L, src) EVAL_THEN(L, src, {});

#define ASSERT_EVAL_FAIL(L, src, err)        \
    {                                        \
        ExecOutput out = luaGD_exec(L, src); \
                                             \
        REQUIRE(out.status != OK);           \
        INFO(out.error.utf8().get_data());   \
        REQUIRE(out.error == err);           \
    }
