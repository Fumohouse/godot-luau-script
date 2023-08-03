#include <catch_amalgamated.hpp>

#include <lua.h>
#include <lualib.h>
#include <godot_cpp/variant/string.hpp>
#include <stdexcept>

#include "luagd_binder.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "test_utils.h"

using namespace godot;

static void test_func(String x, bool y) { // NOLINT
    (void)x;
    (void)y;
}

static String test_func_ret(int p_x, float p_y, bool p_z) {
    Array arr;
    arr.push_back(p_x);
    arr.push_back(p_y);
    arr.push_back(p_z);

    return String("test string: {0} {1} {2}").format(arr);
}

static String test_func_ret_sig(int p_x, float p_y, bool p_z) {
    (void)p_x;
    (void)p_y;
    (void)p_z;

    return "hi";
}

static void test_func_perms() {}

static void test_func_except() {
    throw std::runtime_error("something went wrong!");
}

TEST_CASE_METHOD(LuauFixture, "binder: basic static binding") {
    SECTION("without ret") {
        const char *name = "testFunc";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(FID(test_func), name), name);
        lua_setglobal(L, name);

        ASSERT_EVAL_OK(L, "testFunc('xyz', true)")
        ASSERT_EVAL_FAIL(L, "testFunc({}, 456)", "exec:1: invalid argument #1 to 'testFunc' (string expected, got table)")
    }

    SECTION("with ret") {
        const char *name = "testFuncRet";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(FID(test_func_ret), name), name);
        lua_setglobal(L, name);

        ASSERT_EVAL_EQ(L, "return testFuncRet(1, 0.5, true)", String, "test string: 1 0.5 true")

        SECTION("same signature") {
            // Just to make sure no template oddities are happening
            const char *name_sig = "testFuncRetSig";
            lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(FID(test_func_ret_sig), name_sig), name_sig);
            lua_setglobal(L, name_sig);

            ASSERT_EVAL_EQ(L, "return testFuncRetSig(0, 0, false)", String, "hi")
        }
    }

    SECTION("with perms") {
        const char *name = "testFuncPerms";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(FID(test_func_perms), name, PERMISSION_INTERNAL), name);
        lua_setglobal(L, name);

        ASSERT_EVAL_FAIL(L, "return testFuncPerms()", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'testFuncPerms'. needed permissions: 1, got: 0 !!!")
    }

    SECTION("exception") {
        const char *name = "testFuncExcept";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(FID(test_func_except), name), name);
        lua_setglobal(L, name);

        ASSERT_EVAL_FAIL(L, "return testFuncExcept()", "exec:1: something went wrong!")
    }
}

struct TestClass {
    lua_State *L = nullptr;
    String x;
    float y = 0;
    bool z = true;

    void test_method(lua_State *L, String p_x, float p_y, bool p_z) {
        this->L = L;
        x = p_x; // NOLINT
        y = p_y;
        z = p_z;
    }

    int test_method_ret() {
        return 1;
    }

    int test_method_ret_sig() {
        return 2;
    }

    void test_method_perms() {
    }

    void test_method_except() {
        throw std::runtime_error("something went wrong!");
    }
};

STACK_OP_PTR_DEF(TestClass)
UDATA_STACK_OP_IMPL(TestClass, "Tests.TestClass", DTOR(TestClass))

TEST_CASE_METHOD(LuauFixture, "binder: basic instance binding") {
    luaL_newmetatable(L, "Tests.TestClass");
    lua_pop(L, 1);

    LuaStackOp<TestClass>::push(L, TestClass());
    lua_setglobal(L, "testInstance");

    SECTION("no ret") {
        const char *name = "TestClass.TestMethod";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method(FID(&TestClass::test_method), name), name);
        lua_setglobal(L, "TestMethod");

        EVAL_THEN(L, "return TestMethod(testInstance, 'test', 0.25, false)", {
            lua_getglobal(L, "testInstance");
            TestClass *x = LuaStackOp<TestClass>::check_ptr(L, -1);

            REQUIRE(x->L == L);
            REQUIRE(x->x == "test");
            REQUIRE(x->y == 0.25);
            REQUIRE(!x->z);
        })

        ASSERT_EVAL_FAIL(L, "TestMethod(testInstance)", "exec:1: missing argument #2 to 'TestClass.TestMethod' (string expected)")
    }

    SECTION("with ret") {
        const char *name = "TestClass.TestMethodRet";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method(FID(&TestClass::test_method_ret), name), name);
        lua_setglobal(L, "TestMethodRet");

        ASSERT_EVAL_EQ(L, "return TestMethodRet(testInstance)", int, 1)

        SECTION("same signature") {
            const char *name_sig = "TestClass.TestMethodRetSig";
            lua_pushcfunction(L, LuaGDClassBinder::bind_method(FID(&TestClass::test_method_ret_sig), name_sig), name_sig);
            lua_setglobal(L, "TestMethodRetSig");

            ASSERT_EVAL_EQ(L, "return TestMethodRetSig(testInstance)", int, 2)
        }
    }

    SECTION("with perms") {
        const char *name = "TestClass.TestMethodPerms";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method(FID(&TestClass::test_method_perms), name, PERMISSION_INTERNAL), name);
        lua_setglobal(L, "TestMethodPerms");

        ASSERT_EVAL_FAIL(L, "TestMethodPerms(testInstance)", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'TestClass.TestMethodPerms'. needed permissions: 1, got: 0 !!!")
    }

    SECTION("with exception") {
        const char *name = "TestClass.TestMethodException";
        lua_pushcfunction(L, LuaGDClassBinder::bind_method(FID(&TestClass::test_method_except), name), name);
        lua_setglobal(L, "TestMethodException");

        ASSERT_EVAL_FAIL(L, "TestMethodException(testInstance)", "exec:1: something went wrong!")
    }
}
