#pragma once

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/permissions.h"
#include "core/runtime.h"
#include "core/stack.h"
#include "scripting/luau_script.h"

using namespace godot;

// Variant types are reserved.
// 126-127 are reserved for tests.
enum UserdataTags {
	UDATA_TAG_INT64 = 100,
	UDATA_TAG_CROSS_VM_METHOD = 101,

	UDATA_TAG_LUAU_INTERFACE = 110,
	UDATA_TAG_DEBUG_SERVICE = 111,
	UDATA_TAG_SANDBOX_SERVICE = 112
};

#define luaGD_objnullerror(L, p_i) luaL_error(L, "argument #%d: Object is null or freed", p_i)
#define luaGD_nonamecallatomerror(L) luaL_error(L, "no namecallatom")

#define luaGD_indexerror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid member of %s", p_key, p_of)
#define luaGD_nomethoderror(L, p_key, p_of) luaL_error(L, "'%s' is not a valid method of %s", p_key, p_of)
#define luaGD_valueerror(L, p_key, p_got, p_expected) luaL_error(L, "invalid type for value of key %s: got %s, expected %s", p_key, p_got, p_expected)

#define luaGD_readonlyerror(L, p_type) luaL_error(L, "type '%s' is read-only", p_type)
#define luaGD_propreadonlyerror(L, p_prop) luaL_error(L, "property '%s' is read-only", p_prop)
#define luaGD_propwriteonlyerror(L, p_prop) luaL_error(L, "property '%s' is write-only", p_prop)

struct GDThreadData {
	LuauRuntime::VMType vm_type = LuauRuntime::VM_MAX;
	BitField<ThreadPermissions> permissions = 0;
	Ref<Mutex> lock;
	uint64_t interrupt_deadline = 0;

	Ref<LuauScript> script;
};

lua_State *luaGD_newstate(LuauRuntime::VMType p_vm_type, BitField<ThreadPermissions> p_base_permissions);
lua_State *luaGD_newthread(lua_State *L, BitField<ThreadPermissions> p_permissions);
GDThreadData *luaGD_getthreaddata(lua_State *L);
void luaGD_close(lua_State *L);

bool luaGD_getfield(lua_State *L, int p_index, const char *p_key);

template <typename T>
T luaGD_checkvaluetype(lua_State *L, int p_index, const char *p_key, lua_Type p_texpected) {
	if (!LuaStackOp<T>::is(L, p_index))
		luaGD_valueerror(L, p_key, lua_typename(L, p_texpected), luaL_typename(L, p_index));

	T val = LuaStackOp<T>::get(L, p_index);

	lua_pop(L, 1);
	return val;
}

template <typename T>
T *luaGD_lightudataup(lua_State *L, int p_index) {
	return reinterpret_cast<T *>(
			lua_tolightuserdata(L, lua_upvalueindex(p_index)));
}

void luaGD_gderror(const char *p_method, const String &p_path, String p_msg, int p_line = 0);
const Luau::CompileOptions &luaGD_compileopts();
