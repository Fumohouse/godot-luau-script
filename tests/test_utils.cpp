#include "test_utils.h"

#include <Luau/Compiler.h>
#include <lua.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/string.hpp>
#include <string>

#include "core/lua_utils.h"
#include "core/permissions.h"
#include "core/runtime.h"
#include "core/stack.h"

LuauFixture::LuauFixture() {
	L = luaGD_newstate(LuauRuntime::VM_MAX, PERMISSION_BASE);
}

LuauFixture::~LuauFixture() {
	if (L) {
		luaGD_close(L);
		L = nullptr;
	}
}

ExecOutput luaGD_exec(lua_State *L, const char *p_src) {
	Luau::CompileOptions opts;
	std::string bytecode = Luau::compile(p_src, opts);

	ExecOutput output;

	if (luau_load(L, "=exec", bytecode.data(), bytecode.size(), 0) == 0) {
		int status = lua_resume(L, nullptr, 0);

		if (status == LUA_OK) {
			output.status = OK;
			return output;
		} else if (status == LUA_YIELD) {
			INFO("thread yielded");
			output.status = OK;
			return output;
		}

		String error = LuaStackOp<String>::get(L, -1);
		lua_pop(L, 1);

		output.status = FAILED;
		output.error = error;
		return output;
	}

	output.status = FAILED;
	output.error = LuaStackOp<String>::get(L, -1);
	lua_pop(L, 1);

	return output;
}
