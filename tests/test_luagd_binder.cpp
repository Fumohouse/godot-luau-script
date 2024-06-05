#include <catch_amalgamated.hpp>

#include <lua.h>
#include <lualib.h>
#include <cstddef>
#include <godot_cpp/variant/string.hpp>
#include <stdexcept>

#include "luagd_binder.h"
#include "luagd_lib.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "services/luau_interface.h"
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
		lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(name, FID(test_func)), name);
		lua_setglobal(L, name);

		ASSERT_EVAL_OK(L, "testFunc('xyz', true)")
		ASSERT_EVAL_FAIL(L, "testFunc({}, 456)", "exec:1: invalid argument #1 to 'testFunc' (string expected, got table)")
	}

	SECTION("with ret") {
		const char *name = "testFuncRet";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(name, FID(test_func_ret)), name);
		lua_setglobal(L, name);

		ASSERT_EVAL_EQ(L, "return testFuncRet(1, 0.5, true)", String, "test string: 1 0.5 true")

		SECTION("same signature") {
			// Just to make sure no template oddities are happening
			const char *name_sig = "testFuncRetSig";
			lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(name_sig, FID(test_func_ret_sig)), name_sig);
			lua_setglobal(L, name_sig);

			ASSERT_EVAL_EQ(L, "return testFuncRetSig(0, 0, false)", String, "hi")
		}
	}

	SECTION("with perms") {
		const char *name = "testFuncPerms";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(name, FID(test_func_perms), PERMISSION_INTERNAL), name);
		lua_setglobal(L, name);

		ASSERT_EVAL_FAIL(L, "return testFuncPerms()", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'testFuncPerms'. needed permissions: 1, got: 0 !!!")
	}

	SECTION("exception") {
		const char *name = "testFuncExcept";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method_static(name, FID(test_func_except)), name);
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

STACK_OP_SVC_DEF(TestClass)
SVC_STACK_OP_IMPL(TestClass, "Tests.TestClass")

TEST_CASE_METHOD(LuauFixture, "binder: basic instance binding") {
	luaL_newmetatable(L, "Tests.TestClass");
	lua_pop(L, 1);

	TestClass test_instance;
	LuaStackOp<TestClass *>::push(L, &test_instance);
	lua_setglobal(L, "testInstance");

	SECTION("no ret") {
		const char *name = "TestClass.TestMethod";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method(name, FID(&TestClass::test_method)), name);
		lua_setglobal(L, "TestMethod");

		EVAL_THEN(L, "return TestMethod(testInstance, 'test', 0.25, false)", {
			REQUIRE(test_instance.L == L);
			REQUIRE(test_instance.x == "test");
			REQUIRE(test_instance.y == 0.25);
			REQUIRE(!test_instance.z);
		})

		ASSERT_EVAL_FAIL(L, "TestMethod(testInstance)", "exec:1: missing argument #2 to 'TestClass.TestMethod' (string expected)")
	}

	SECTION("with ret") {
		const char *name = "TestClass.TestMethodRet";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method(name, FID(&TestClass::test_method_ret)), name);
		lua_setglobal(L, "TestMethodRet");

		ASSERT_EVAL_EQ(L, "return TestMethodRet(testInstance)", int, 1)

		SECTION("same signature") {
			const char *name_sig = "TestClass.TestMethodRetSig";
			lua_pushcfunction(L, LuaGDClassBinder::bind_method(name_sig, FID(&TestClass::test_method_ret_sig)), name_sig);
			lua_setglobal(L, "TestMethodRetSig");

			ASSERT_EVAL_EQ(L, "return TestMethodRetSig(testInstance)", int, 2)
		}
	}

	SECTION("with perms") {
		const char *name = "TestClass.TestMethodPerms";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method(name, FID(&TestClass::test_method_perms), PERMISSION_INTERNAL), name);
		lua_setglobal(L, "TestMethodPerms");

		ASSERT_EVAL_FAIL(L, "TestMethodPerms(testInstance)", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'TestClass.TestMethodPerms'. needed permissions: 1, got: 0 !!!")
	}

	SECTION("with exception") {
		const char *name = "TestClass.TestMethodException";
		lua_pushcfunction(L, LuaGDClassBinder::bind_method(name, FID(&TestClass::test_method_except)), name);
		lua_setglobal(L, "TestMethodException");

		ASSERT_EVAL_FAIL(L, "TestMethodException(testInstance)", "exec:1: something went wrong!")
	}
}

struct TestFullClass {
	int test_prop = 10;

	int x = 0;
	bool y = false;

	static int test_static_method() {
		return 5;
	}

	void test_method(int p_x, bool p_y) {
		x = p_x;
		y = p_y;
	}

	void set_test_prop(int p_value) {
		test_prop = p_value;
	}

	int get_test_prop() const {
		return test_prop;
	}

	int get_readonly_prop() const {
		return 1;
	}

	void set_writeonly_prop(int p_value) {
		x = p_value * 2;
	}
};

STACK_OP_SVC_DEF(TestFullClass)
SVC_STACK_OP_IMPL(TestFullClass, "Tests.TestFullClass")

TEST_CASE_METHOD(LuauFixture, "binder: class binding") {
	LuaGDClass test_class;
	test_class.set_name("TestFullClass", "Tests.TestFullClass");

	test_class.bind_method_static("TestStaticMethod", FID(TestFullClass::test_static_method), PERMISSION_INTERNAL);
	test_class.bind_method("TestMethod", FID(&TestFullClass::test_method), PERMISSION_INTERNAL);

	lua_CFunction set_test = test_class.bind_method("SetTestProp", FID(&TestFullClass::set_test_prop));
	lua_CFunction get_test = test_class.bind_method("GetTestProp", FID(&TestFullClass::get_test_prop));
	test_class.bind_property("testProp", set_test, get_test);

	lua_CFunction get_readonly = test_class.bind_method("GetReadonly", FID(&TestFullClass::get_readonly_prop));
	test_class.bind_property("testReadonly", nullptr, get_readonly);

	lua_CFunction set_writeonly = test_class.bind_method("SetWriteonly", FID(&TestFullClass::set_writeonly_prop));
	test_class.bind_property("testWriteonly", set_writeonly, nullptr);

	test_class.init_metatable(L);

	TestFullClass test_instance;
	LuaStackOp<TestFullClass *>::push(L, &test_instance);
	lua_setglobal(L, "testInstance");

	GDThreadData *udata = luaGD_getthreaddata(L);

	SECTION("static method") {
		udata->permissions = PERMISSION_INTERNAL;
		ASSERT_EVAL_EQ(L, "return testInstance.TestStaticMethod()", int, 5)

		udata->permissions = PERMISSION_BASE;
		ASSERT_EVAL_FAIL(L, "testInstance.TestStaticMethod()", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'Tests.TestFullClass.TestStaticMethod'. needed permissions: 1, got: 0 !!!")
	}

	SECTION("instance method") {
		udata->permissions = PERMISSION_INTERNAL;
		EVAL_THEN(L, "testInstance:TestMethod(1, true)", {
			REQUIRE(test_instance.x == 1);
			REQUIRE(test_instance.y);
		})

		udata->permissions = PERMISSION_BASE;
		ASSERT_EVAL_FAIL(L, "testInstance:TestMethod(1, true)", "exec:1: !!! THREAD PERMISSION VIOLATION: attempted to access 'Tests.TestFullClass.TestMethod'. needed permissions: 1, got: 0 !!!")
	}

	SECTION("property") {
		SECTION("normal") {
			ASSERT_EVAL_OK(L, "testInstance.testProp = 2");
			ASSERT_EVAL_EQ(L, "return testInstance.testProp", int, 2)
		}

		SECTION("read-only") {
			ASSERT_EVAL_EQ(L, "return testInstance.testReadonly", int, 1)
			ASSERT_EVAL_FAIL(L, "testInstance.testReadonly = 5", "exec:1: property 'testReadonly' is read-only");
		}

		SECTION("write-only") {
			EVAL_THEN(L, "testInstance.testWriteonly = 2", {
				REQUIRE(test_instance.x == 4);
			})

			ASSERT_EVAL_FAIL(L, "return testInstance.testWriteonly", "exec:1: property 'testWriteonly' is write-only");
		}
	}
}
