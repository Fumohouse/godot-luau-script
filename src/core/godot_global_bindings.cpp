#include "core/godot_bindings.h"

#include <lua.h>
#include <lualib.h>

#include "core/extension_api.h"
#include "core/lua_utils.h"
#include "core/permissions.h"
#include "core/variant.h"

using namespace godot;

static int luaGD_utility_function(lua_State *L) {
	const ApiUtilityFunction *func = luaGD_lightudataup<ApiUtilityFunction>(L, 1);

	const GDThreadStack &stack = get_arguments<ApiUtilityFunction, ApiArgumentNoDefault>(L, func->name, *func);

	if (func->return_type == -1) {
		SET_CALL_STACK(L);
		func->func(nullptr, stack.ptr_args, stack.size);
		CLEAR_CALL_STACK;
		return 0;
	} else {
		LuauVariant ret;
		ret.initialize(GDExtensionVariantType(func->return_type));

		SET_CALL_STACK(L);
		func->func(ret.get_opaque_pointer(), stack.ptr_args, stack.size);
		CLEAR_CALL_STACK;

		ret.lua_push(L);
		return 1;
	}
}

static int luaGD_print_function(lua_State *L) {
	GDExtensionPtrUtilityFunction func = (GDExtensionPtrUtilityFunction)lua_tolightuserdata(L, lua_upvalueindex(1));
	GDThreadStack &stack = *luaGD_getthreaddata(L)->stack;

	int nargs = lua_gettop(L);

	stack.resize(nargs);

	for (int i = 0; i < nargs; i++) {
		if (LuaStackOp<Variant>::is(L, i + 1)) {
			stack.varargs[i] = LuaStackOp<Variant>::get(L, i + 1);
		} else {
			stack.varargs[i] = luaL_tolstring(L, i + 1, nullptr);
		}

		stack.ptr_args[i] = &stack.varargs[i];
	}

	func(nullptr, stack.ptr_args, stack.size);
	return 0;
}

void luaGD_openglobals(lua_State *L) {
	LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")

	const ExtensionApi &api = get_extension_api();

	// Enum
	lua_createtable(L, 0, api.global_enums.size() + 3);

	for (const ApiEnum &global_enum : api.global_enums) {
		push_enum(L, global_enum);
		lua_setfield(L, -2, global_enum.name);
	}

	push_enum(L, get_permissions_enum());
	lua_setfield(L, -2, get_permissions_enum().name);

	lua_createtable(L, 0, 1);

	lua_pushstring(L, "Enum");
	lua_pushcclosure(L, luaGD_global_index, "Enum.__index", 1);
	lua_setfield(L, -2, "__index");

	lua_setreadonly(L, -1, true);
	lua_setmetatable(L, -2);

	lua_setreadonly(L, -1, true);
	lua_setglobal(L, "Enum");

	// Constants
	// does this work? idk
	lua_createtable(L, 0, api.global_constants.size());

	for (const ApiConstant &global_constant : api.global_constants) {
		lua_pushinteger(L, global_constant.value);
		lua_setfield(L, -2, global_constant.name);
	}

	lua_createtable(L, 0, 1);

	lua_pushstring(L, "Constants");
	lua_pushcclosure(L, luaGD_global_index, "Constants.__index", 1);
	lua_setfield(L, -2, "__index");

	lua_setreadonly(L, -1, true);
	lua_setmetatable(L, -2);

	lua_setreadonly(L, -1, true);
	lua_setglobal(L, "Constants");

	// Utility functions
	for (const ApiUtilityFunction &utility_function : api.utility_functions) {
		if (utility_function.is_print_func) {
			lua_pushlightuserdata(L, (void *)utility_function.func);
			lua_pushcclosure(L, luaGD_print_function, utility_function.debug_name, 1);
		} else {
			lua_pushlightuserdata(L, (void *)&utility_function);
			lua_pushcclosure(L, luaGD_utility_function, utility_function.debug_name, 1);
		}

		lua_setglobal(L, utility_function.name);
	}
}
