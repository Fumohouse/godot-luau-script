#pragma once

#include <gdextension_interface.h>
#include <lua.h>
#include <exception>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/method_ptrcall.hpp> // TODO: unused. required to prevent compile error when specializing PtrToArg.
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <type_traits>
#include <utility>

#include "core/permissions.h"
#include "core/stack.h"

using namespace godot;

class LuaGDError : public std::exception {};

// Concept similar to Godot's ClassDB implementation
// (https://github.com/godotengine/godot/blob/master/core/variant/binder_common.h)

class LuaGDClassBinder {
	template <typename P, typename Pc = std::remove_const_t<std::remove_reference_t<P>>>
	static Pc check_arg(lua_State *L, int &p_idx) {
		return LuaStackOp<Pc>::check(L, p_idx++);
	}

	// https://stackoverflow.com/a/48368508
	template <typename F>
	static lua_CFunction cify_lambda(F &&p_func) {
		// std::forward ensures no copy is made (or something)
		// https://en.cppreference.com/w/cpp/utility/forward
		static F func = std::forward<F>(p_func);

		return [](lua_State *L) -> int {
			return func(L);
		};
	}

public:
	// Prevent two functions/methods with the same signature from interfering
	// with each other. Can't think of a better way to do it, so this is
	// probably ok.
	template <auto F, typename TF = decltype(F)>
	struct FunctionId {};

	template <auto F, typename R, typename... P>
	static lua_CFunction bind_method_static(const String &p_name, FunctionId<F, R (*)(P...)>, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) {
		return cify_lambda([=](lua_State *L) -> int {
			static CharString name = p_name.utf8();
			luaGD_checkpermissions(L, name.get_data(), p_perms);

			try {
				int stack_idx = 1;
				if constexpr (std::is_same<R, void>()) {
					F(check_arg<P>(L, stack_idx)...);
					return 0;
				} else {
					LuaStackOp<R>::push(L, F(check_arg<P>(L, stack_idx)...));
					return 1;
				}
			} catch (LuaGDError &e) {
				luaL_error(L, "%s", e.what());
			}
		});
	}

#define BIND_METHOD_METHOD(m_name, m_qualifiers)                                                                                                           \
	template <auto F, typename T, typename R, typename... P>                                                                                               \
	static lua_CFunction m_name(const String &p_name, FunctionId<F, R (T::*)(P...) m_qualifiers>, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) { \
		return cify_lambda([=](lua_State *L) -> int {                                                                                                      \
			static CharString name = p_name.utf8();                                                                                                        \
			luaGD_checkpermissions(L, name.get_data(), p_perms);                                                                                           \
                                                                                                                                                           \
			T *self = LuaStackOp<T *>::check(L, 1);                                                                                                        \
                                                                                                                                                           \
			try {                                                                                                                                          \
				int stack_idx = 2;                                                                                                                         \
				if constexpr (std::is_same<R, void>()) {                                                                                                   \
					(self->*F)(check_arg<P>(L, stack_idx)...);                                                                                             \
					return 0;                                                                                                                              \
				} else {                                                                                                                                   \
					LuaStackOp<R>::push(L, (self->*F)(check_arg<P>(L, stack_idx)...));                                                                     \
					return 1;                                                                                                                              \
				}                                                                                                                                          \
			} catch (LuaGDError & e) {                                                                                                                     \
				luaL_error(L, "%s", e.what());                                                                                                             \
			}                                                                                                                                              \
		});                                                                                                                                                \
	}

	BIND_METHOD_METHOD(bind_method, )
	BIND_METHOD_METHOD(bind_method, const)
};

#define FID(m_func) LuaGDClassBinder::FunctionId<m_func>()

class LuaGDClass {
	struct Method {
		lua_CFunction func;
		String debug_name;
	};

	struct Property {
		lua_CFunction setter;
		lua_CFunction getter;
	};

	typedef int (*IndexOverride)(lua_State *L, const char *p_name);

	const char *name = nullptr;
	const char *metatable_name = nullptr;
	IndexOverride index_override = nullptr;

	HashMap<String, Method> static_methods;
	HashMap<String, Method> methods;
	HashMap<String, Property> properties;

	String get_debug_name(const String &p_name);

	static int lua_namecall(lua_State *L);
	static int lua_newindex(lua_State *L);
	static int lua_index(lua_State *L);

public:
	void set_name(const char *p_name, const char *p_metatable_name);
	const char *get_name() const { return name; }

	template <typename FId>
	lua_CFunction bind_method_static(const char *p_name, FId p_func, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) {
		String debug_name = get_debug_name(p_name);
		lua_CFunction lua_func = LuaGDClassBinder::bind_method_static(debug_name, p_func, p_perms);
		static_methods.insert(p_name, { lua_func, debug_name });

		return lua_func;
	}

	template <typename FId>
	lua_CFunction bind_method(const char *p_name, FId p_func, BitField<ThreadPermissions> p_perms = PERMISSION_BASE) {
		String debug_name = get_debug_name(p_name);
		lua_CFunction lua_func = LuaGDClassBinder::bind_method(debug_name, p_func, p_perms);
		methods.insert(p_name, { lua_func, debug_name });

		return lua_func;
	}

	void bind_property(const char *p_name, lua_CFunction setter, lua_CFunction getter);

	void set_index_override(IndexOverride p_func);

	// Must be called when this LuaGDClass's pointer is stable
	void init_metatable(lua_State *L) const;
};

template <>
inline lua_State *LuaGDClassBinder::check_arg<lua_State *>(lua_State *L, int &p_idx) {
	return L;
}

template <>
inline GDExtensionObjectPtr LuaGDClassBinder::check_arg<GDExtensionObjectPtr>(lua_State *L, int &p_idx) {
	return LuaStackOp<Object *>::check(L, p_idx++);
}
