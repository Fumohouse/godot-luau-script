#include "core/godot_bindings.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <type_traits>

#include "core/extension_api.h"
#include "core/lua_utils.h"
#include "core/permissions.h"
#include "core/stack.h"
#include "core/variant.h"
#include "scripting/luau_lib.h"

int luaGD_global_index(lua_State *L) {
	const char *name = lua_tostring(L, lua_upvalueindex(1));
	luaGD_indexerror(L, luaL_checkstring(L, 2), name);

	return 1;
}

void push_enum(lua_State *L, const ApiEnum &p_enum) {
	lua_createtable(L, 0, p_enum.values.size());

	for (const Pair<String, int32_t> &value : p_enum.values) {
		lua_pushinteger(L, value.second);
		lua_setfield(L, -2, value.first.utf8().get_data());
	}

	lua_createtable(L, 0, 1);

	lua_pushstring(L, p_enum.name);
	lua_pushcclosure(L, luaGD_global_index, "Enum.__index", 1); // TODO: name
	lua_setfield(L, -2, "__index");

	lua_setreadonly(L, -1, true);
	lua_setmetatable(L, -2);

	lua_setreadonly(L, -1, true);
}

/* GETTING ARGUMENTS */

template <typename T>
_FORCE_INLINE_ static void get_argument(lua_State *L, int p_idx, const T &p_arg, LuauVariant &r_out) {
	r_out.lua_check(L, p_idx, p_arg.get_arg_type(), p_arg.get_arg_type_name());
}

// Defaults
template <typename T>
struct has_default_value_trait : std::true_type {};

template <>
struct has_default_value_trait<ApiArgumentNoDefault> : std::false_type {};

template <typename TMethod, typename TArg>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int p_arg_offset, int p_nargs, LocalVector<const void *> *pargs, const TMethod &p_method, std::true_type const &) {
	for (int i = p_nargs; i < p_method.arguments.size(); i++) {
		const TArg &arg = p_method.arguments[i];

		if (arg.has_default_value) {
			// Special case: null Object default value
			if (arg.get_arg_type() == GDEXTENSION_VARIANT_TYPE_OBJECT) {
				if (*arg.default_value.template get_ptr<GDExtensionObjectPtr>()) {
					// Should never happen
					ERR_PRINT("Could not set non-null object argument default value");
				}

				pargs->ptr()[i] = nullptr;
			} else {
				pargs->ptr()[i] = arg.default_value.get_opaque_pointer();
			}
		} else {
			LuauVariant dummy;
			get_argument(L, i + 1 + p_arg_offset, arg, dummy);
		}
	}
}

template <>
_FORCE_INLINE_ void get_default_args<GDMethod, GDProperty>(
		lua_State *L, int p_arg_offset, int p_nargs, LocalVector<const void *> *pargs, const GDMethod &p_method, std::true_type const &) {
	int args_allowed = p_method.arguments.size();
	int args_default = p_method.default_arguments.size();
	int args_required = args_allowed - args_default;

	for (int i = p_nargs; i < args_allowed; i++) {
		const GDProperty &arg = p_method.arguments[i];

		if (i >= args_required) {
			pargs->ptr()[i] = &p_method.default_arguments[i - args_required];
		} else {
			LuauVariant dummy;
			get_argument(L, i + 1 + p_arg_offset, arg, dummy);
		}
	}
}

template <typename TMethod, typename>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int p_arg_offset, int p_nargs, LocalVector<const void *> *pargs, const TMethod &p_method, std::false_type const &) {
	LuauVariant dummy;
	get_argument(L, p_nargs + 1 + p_arg_offset, p_method.arguments[p_nargs], dummy);
}

// this is magic
// Gets arguments from stack for some type of function.
// Magic functions:
// - T::is_method_static
// - T::is_method_vararg
// - TArg::get_arg_type
// - TArg::get_arg_type_name
template <typename T, typename TArg>
int get_arguments(lua_State *L,
		const char *p_method_name,
		LocalVector<Variant> *r_varargs,
		LocalVector<LuauVariant> *r_args,
		LocalVector<const void *> *r_pargs,
		const T &p_method) {
	// arg 1 is self for instance methods
	int arg_offset = p_method.is_method_static() ? 0 : 1;
	int nargs = lua_gettop(L) - arg_offset;

	if (p_method.arguments.size() > nargs)
		r_pargs->resize(p_method.arguments.size());
	else
		r_pargs->resize(nargs);

	if (p_method.is_method_vararg()) {
		r_varargs->resize(nargs);

		LuauVariant arg;

		for (int i = 0; i < nargs; i++) {
			if (i < p_method.arguments.size()) {
				get_argument(L, i + 1 + arg_offset, p_method.arguments[i], arg);
				r_varargs->ptr()[i] = arg.to_variant();
			} else {
				r_varargs->ptr()[i] = LuaStackOp<Variant>::check(L, i + 1 + arg_offset);
			}

			r_pargs->ptr()[i] = &r_varargs->ptr()[i];
		}
	} else {
		r_args->resize(nargs);

		if (nargs > p_method.arguments.size())
			luaL_error(L, "too many arguments to '%s' (expected at most %ld)", p_method_name, p_method.arguments.size());

		for (int i = 0; i < nargs; i++) {
			get_argument(L, i + 1 + arg_offset, p_method.arguments[i], r_args->ptr()[i]);
			r_pargs->ptr()[i] = r_args->ptr()[i].get_opaque_pointer();
		}
	}

	if (nargs < p_method.arguments.size())
		get_default_args<T, TArg>(L, arg_offset, nargs, r_pargs, p_method, has_default_value_trait<TArg>());

	return nargs;
}

#define ARGUMENT_TYPE(m_method, m_arg)                         \
	template int get_arguments<m_method, m_arg>(lua_State * L, \
			const char *p_method_name,                         \
			LocalVector<Variant> *r_varargs,                   \
			LocalVector<LuauVariant> *r_args,                  \
			LocalVector<const void *> *r_pargs,                \
			const m_method &p_method);

ARGUMENT_TYPE(ApiVariantMethod, ApiArgument); // Godot builtin classes (e.g. Vector2)
ARGUMENT_TYPE(ApiClassMethod, ApiClassArgument); // Godot object classes (e.g. RefCounted)
ARGUMENT_TYPE(ApiUtilityFunction, ApiArgumentNoDefault); // Godot utility functions (e.g. lerp)
ARGUMENT_TYPE(GDMethod, GDProperty); // User-defined classes

//////////////////////////
// Builtin/class common //
//////////////////////////

int luaGD_variant_tostring(lua_State *L) {
	// Special case - freed objects
	if (LuaStackOp<Object *>::is(L, 1) && !LuaStackOp<Object *>::get(L, 1)) {
		lua_pushstring(L, "<Freed Object>");
	} else {
		Variant v = LuaStackOp<Variant>::check(L, 1);
		String str = v.stringify();
		lua_pushstring(L, str.utf8().get_data());
	}

	return 1;
}

void luaGD_initmetatable(lua_State *L, int p_idx, GDExtensionVariantType p_variant_type, const char *p_global_name) {
	p_idx = lua_absindex(L, p_idx);

	// for typeof and type errors
	lua_pushstring(L, p_global_name);
	lua_setfield(L, p_idx, "__type");

	lua_pushcfunction(L, luaGD_variant_tostring, VARIANT_TOSTRING_DEBUG_NAME);
	lua_setfield(L, p_idx, "__tostring");

	// Variant type ID
	lua_pushinteger(L, p_variant_type);
	lua_setfield(L, p_idx, MT_VARIANT_TYPE);
}

void luaGD_initglobaltable(lua_State *L, int p_idx, const char *p_global_name) {
	p_idx = lua_absindex(L, p_idx);

	lua_pushvalue(L, p_idx);
	lua_setglobal(L, p_global_name);

	lua_createtable(L, 0, 2);

	lua_pushstring(L, p_global_name);
	lua_setfield(L, -2, MT_CLASS_GLOBAL);

	lua_pushstring(L, p_global_name);
	lua_pushcclosure(L, luaGD_global_index, "_G.__index", 1); // TODO: name
	lua_setfield(L, -2, "__index");

	lua_setreadonly(L, -1, true);

	lua_setmetatable(L, p_idx);
}

ThreadPermissions get_method_permissions(const ApiClass &p_class, const ApiClassMethod &p_method) {
	return p_method.permissions != PERMISSION_INHERIT ? p_method.permissions : p_class.default_permissions;
}
