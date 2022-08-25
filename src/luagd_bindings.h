#pragma once

#include <lua.h>
#include <unordered_map>
#include <string>
#include <godot_cpp/templates/vector.hpp>

#include "luagd.h"

// TODO: May want to use Godot's HashMap, except right now the godot-cpp version doesn't compile
typedef std::unordered_map<std::string, lua_CFunction> MethodMap;

struct ClassInfo
{
    int parent_idx = -1;

    MethodMap methods;
    MethodMap static_funcs;
};

typedef Vector<ClassInfo> ClassRegistry;

#define LUA_BUILTIN_CONST(variant_type, const_name, const_type)                                                              \
    [](lua_State *L) -> int                                                                                                  \
    {                                                                                                                        \
        static bool __did_init;                                                                                              \
        static Variant __const_value;                                                                                        \
                                                                                                                             \
        if (!__did_init)                                                                                                     \
        {                                                                                                                    \
            __did_init = true;                                                                                               \
            internal::gdn_interface->variant_get_constant_value(GDNATIVE_VARIANT_TYPE_VECTOR2, #const_name, &__const_value); \
        }                                                                                                                    \
                                                                                                                             \
        LuaStackOp<const_type>::push(L, __const_value);                                                                      \
        return 1;                                                                                                            \
    }

// ! engine_ptrcall.hpp
template <class O, class... Args>
O *_call_native_mb_ret_obj_arr(const GDNativeMethodBindPtr mb, void *instance, const GDNativeTypePtr *args) {
	GodotObject *ret = nullptr;
	internal::gdn_interface->object_method_bind_ptrcall(mb, instance, args, &ret);

	if (ret == nullptr) {
		return nullptr;
	}

	return reinterpret_cast<O *>(internal::gdn_interface->object_get_instance_binding(ret, internal::token, &O::___binding_callbacks));
}

// The corresponding source files for these methods are generated.
void luaGD_openbuiltins(lua_State *L);
void luaGD_openclasses(lua_State *L);

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name);
void luaGD_poplib(lua_State *L, bool is_obj);

int luaGD_builtin_namecall(lua_State *L);
int luaGD_builtin_global_index(lua_State *L);

int luaGD_class_ctor(lua_State *L);
int luaGD_class_no_ctor(lua_State *L);

int luaGD_class_namecall(lua_State *L);
int luaGD_class_global_index(lua_State *L);
