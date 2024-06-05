#include <catch_amalgamated.hpp>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/memory.hpp>

#include "luagd_stack.h"
#include "test_utils.h"

TEST_CASE_METHOD(LuauFixture, "benchmarks: object stack operations") {
	BENCHMARK("object: 1 object push and pop once") {
		Object *obj = memnew(Object);
		LuaStackOp<Object *>::push(L, obj);
		lua_pop(L, 1);
		memdelete(obj);
	};

	Object *objs[10000];
	for (int i = 0; i < 10000; i++) {
		objs[i] = memnew(Object);
	}

	BENCHMARK("object: 10000 objects push and pop many times") {
		for (int i = 0; i < 10000; i++) {
			LuaStackOp<Object *>::push(L, objs[i]);
			lua_pop(L, 1);
		}
	};

	for (int i = 0; i < 10000; i++) {
		memdelete(objs[i]);
	}
}

TEST_CASE_METHOD(LuauFixture, "benchmarks: class bindings") {
	const char *src = "return PhysicsServer3D.singleton:GetClass()";
	ASSERT_EVAL_OK(L, src)

	BENCHMARK("singleton call") {
		luaGD_exec(L, src);
	};
}
