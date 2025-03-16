#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <godot_cpp/variant/variant.hpp>

#include "core/permissions.h"

using namespace godot;

struct ApiClass;
struct ApiClassMethod;
struct ApiEnum;
struct GDThreadStack;

#define BUILTIN_MT_PREFIX "Godot.Builtin."
#define BUILTIN_MT_NAME(m_type) BUILTIN_MT_PREFIX #m_type

#define MT_VARIANT_TYPE "__gdvariant"
#define MT_CLASS_TYPE "__gdclass"
#define MT_CLASS_GLOBAL "__classglobal"

#define LUAGD_LOAD_GUARD(L, m_key)             \
	lua_getfield(L, LUA_REGISTRYINDEX, m_key); \
                                               \
	if (!lua_isnil(L, -1))                     \
		return;                                \
                                               \
	lua_pop(L, 1);                             \
                                               \
	lua_pushboolean(L, true);                  \
	lua_setfield(L, LUA_REGISTRYINDEX, m_key);

#if TOOLS_ENABLED
#define SET_CALL_STACK(L) LuauLanguage::get_singleton()->set_call_stack(L)
#define CLEAR_CALL_STACK LuauLanguage::get_singleton()->clear_call_stack()
#else
#define SET_CALL_STACK(L)
#define CLEAR_CALL_STACK
#endif

int luaGD_global_index(lua_State *L);
void push_enum(lua_State *L, const ApiEnum &p_enum);

#define VARIANT_TOSTRING_DEBUG_NAME "Variant.__tostring"
int luaGD_variant_tostring(lua_State *L);
void luaGD_initmetatable(lua_State *L, int p_idx, GDExtensionVariantType p_variant_type, const char *p_global_name);
void luaGD_initglobaltable(lua_State *L, int p_idx, const char *p_global_name);
ThreadPermissions get_method_permissions(const ApiClass &p_class, const ApiClassMethod &p_method);

template <typename T, typename TArg>
const GDThreadStack &get_arguments(lua_State *L,
		const char *p_method_name,
		const T &p_method);

void luaGD_openbuiltins(lua_State *L);
void luaGD_openclasses(lua_State *L);
void luaGD_openglobals(lua_State *L);
