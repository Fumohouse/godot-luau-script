#include "core/godot_bindings.h"

#include <gdextension_interface.h>
#include <lualib.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

#include "core/extension_api.h"
#include "core/lua_utils.h"
#include "core/stack.h"
#include "utils/wrapped_no_binding.h"

/* SPECIAL ARRAY BEHAVIOR */

template <typename T>
static int luaGD_array_len(lua_State *L) {
	T *array = LuaStackOp<T>::check_ptr(L, 1);
	lua_pushinteger(L, array->size());

	return 1;
}

template <typename TArray, typename TElem>
static int luaGD_array_next(lua_State *L) {
	TArray *array = LuaStackOp<TArray>::check_ptr(L, 1);
	int idx = luaL_checkinteger(L, 2);
	idx++;

	if (idx >= array->size()) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, idx);
	LuaStackOp<TElem>::push(L, array->operator[](idx));
	return 2;
}

static int luaGD_array_iter(lua_State *L) {
	lua_pushvalue(L, lua_upvalueindex(1)); // next
	lua_pushvalue(L, 1); // array
	lua_pushinteger(L, -1); // initial index

	return 3;
}

struct ArrayTypeInfo {
	const char *len_debug_name;
	lua_CFunction len;

	const char *iter_next_debug_name;
	lua_CFunction iter_next;
};

#define ARRAY_INFO(m_type, m_elem_type)           \
	{                                             \
		static const ArrayTypeInfo type##_info{   \
			BUILTIN_MT_NAME(m_type) ".__len",     \
			luaGD_array_len<m_type>,              \
			BUILTIN_MT_NAME(m_type) ".next",      \
			luaGD_array_next<m_type, m_elem_type> \
		};                                        \
                                                  \
		return &type##_info;                      \
	}

// ! sync with any new arrays
static const ArrayTypeInfo *get_array_type_info(GDExtensionVariantType p_type) {
	switch (p_type) {
		case GDEXTENSION_VARIANT_TYPE_ARRAY:
			ARRAY_INFO(Array, Variant)
		case GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY:
			ARRAY_INFO(PackedByteArray, uint8_t)
		case GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
			ARRAY_INFO(PackedInt32Array, int32_t)
		case GDEXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
			ARRAY_INFO(PackedInt64Array, int64_t)
		case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
			ARRAY_INFO(PackedFloat32Array, float)
		case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
			ARRAY_INFO(PackedFloat64Array, double)
		case GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
			ARRAY_INFO(PackedStringArray, String)
		case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
			ARRAY_INFO(PackedVector2Array, Vector2)
		case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
			ARRAY_INFO(PackedVector3Array, Vector3)
		case GDEXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY:
			ARRAY_INFO(PackedColorArray, Color)
		case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR4_ARRAY:
			ARRAY_INFO(PackedVector4Array, Vector4)

		default:
			return nullptr;
	}
}

/* DICTIONARY ITERATION */

static int luaGD_dict_next(lua_State *L) {
	Dictionary *dict = LuaStackOp<Dictionary>::check_ptr(L, 1);
	Variant key = LuaStackOp<Variant>::check(L, 2);

	Array keys = dict->keys();
	int sz = keys.size();
	int new_idx = -1;

	if (key == Variant()) {
		// Begin iteration.
		new_idx = 0;
	} else {
		for (int i = 0; i < sz; i++) {
			if (keys[i] == key) {
				new_idx = i + 1;
				break;
			}
		}
	}

	if (new_idx == sz) {
		// Iteration finished.
		lua_pushnil(L);
		return 1;
	}

	if (new_idx >= 0) {
		const Variant &new_key = keys[new_idx];

		LuaStackOp<Variant>::push(L, new_key);
		LuaStackOp<Variant>::push(L, dict->operator[](new_key)); // new value
		return 2;
	}

	luaL_error(L, "could not find previous key in dictionary: did you erase its value during iteration?");
}

static int luaGD_dict_iter(lua_State *L) {
	lua_pushvalue(L, lua_upvalueindex(1)); // next
	lua_pushvalue(L, 1); // dict
	lua_pushnil(L); // initial key

	return 3;
}

static int luaGD_builtin_ctor(lua_State *L) {
	const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
	const char *error_string = lua_tostring(L, lua_upvalueindex(2));

	int nargs = lua_gettop(L);

	LocalVector<LuauVariant> args;
	args.resize(nargs);

	LocalVector<const void *> pargs;
	pargs.resize(nargs);

	for (const ApiVariantConstructor &ctor : builtin_class->constructors) {
		if (nargs != ctor.arguments.size())
			continue;

		bool valid = true;

		for (int i = 0; i < nargs; i++) {
			GDExtensionVariantType type = ctor.arguments[i].type;

			if (!LuauVariant::lua_is(L, i + 1, type)) {
				valid = false;
				break;
			}

			args[i].lua_check(L, i + 1, type);
			pargs[i] = args[i].get_opaque_pointer();
		}

		if (!valid)
			continue;

		LuauVariant ret;
		ret.initialize(builtin_class->type);

		ctor.func(ret.get_opaque_pointer(), pargs.ptr());

		ret.lua_push(L);
		return 1;
	}

	luaL_error(L, "%s", error_string);
}

static int luaGD_callable_ctor(lua_State *L) {
	const Vector<ApiClass> &classes = get_extension_api().classes;

	int class_idx = -1;

	{
		// Get class index.
		if (lua_getmetatable(L, 1)) {
			lua_getfield(L, -1, MT_CLASS_TYPE);

			if (lua_isnumber(L, -1)) {
				class_idx = lua_tointeger(L, -1);
			}

			lua_pop(L, 2); // value, metatable
		}

		if (class_idx == -1)
			luaL_typeerrorL(L, 1, "Object");
	}

	GDExtensionObjectPtr self = LuaStackOp<Object *>::get(L, 1);
	const char *method = luaL_checkstring(L, 2);

	// Check if instance has method.
	if (LuauScriptInstance *inst = LuauScriptInstance::from_object(self)) {
		if (inst->has_method(method)) {
			nb::Object self_obj = self;
			LuaStackOp<Callable>::push(L, Callable(&self_obj, method));
			return 1;
		}
	}

	// Check if class or its parents have method.
	while (class_idx != -1) {
		const ApiClass &g_class = classes.get(class_idx);

		HashMap<String, ApiClassMethod>::ConstIterator E = g_class.methods.find(method);

		if (E) {
			luaGD_checkpermissions(L, E->value.debug_name, get_method_permissions(g_class, E->value));

			nb::Object self_obj = self;
			LuaStackOp<Callable>::push(L, Callable(&self_obj, E->value.gd_name));
			return 1;
		}

		class_idx = g_class.parent_idx;
	}

	luaGD_nomethoderror(L, method, "this object");
}

static int luaGD_builtin_newindex(lua_State *L) {
	const char *name = lua_tostring(L, lua_upvalueindex(1));
	luaGD_readonlyerror(L, name);
}

static int luaGD_builtin_index(lua_State *L) {
	const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

	LuauVariant self;
	self.lua_check(L, 1, builtin_class->type);

	String key = LuaStackOp<String>::check(L, 2);

	// Members
	if (builtin_class->members.has(key)) {
		const ApiVariantMember &member = builtin_class->members.get(key);

		LuauVariant ret;
		ret.initialize(member.type);

		member.getter(self.get_opaque_pointer(), ret.get_opaque_pointer());

		ret.lua_push(L);
		return 1;
	}

	luaGD_indexerror(L, key.utf8().get_data(), builtin_class->name);
}

static int call_builtin_method(lua_State *L, const ApiBuiltinClass &p_builtin_class, const ApiVariantMethod &p_method) {
	LocalVector<Variant> varargs;
	LocalVector<LuauVariant> args;
	LocalVector<const void *> pargs;

	get_arguments<ApiVariantMethod, ApiArgument>(L, p_method.name, &varargs, &args, &pargs, p_method);

	if (p_method.is_vararg) {
		Variant ret;

		if (p_method.is_static) {
			SET_CALL_STACK(L);
			internal::gdextension_interface_variant_call_static(p_builtin_class.type, &p_method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);
			CLEAR_CALL_STACK;
		} else {
			Variant self = LuaStackOp<Variant>::check(L, 1);
			SET_CALL_STACK(L);
			internal::gdextension_interface_variant_call(&self, &p_method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);
			CLEAR_CALL_STACK;

			// HACK: since the value in self is copied,
			// it's necessary to manually assign the changed value back to Luau
			if (!p_method.is_const) {
				LuauVariant lua_self;
				lua_self.lua_check(L, 1, p_builtin_class.type);
				lua_self.assign_variant(self);
			}
		}

		if (p_method.return_type != GDEXTENSION_VARIANT_TYPE_NIL) {
			LuaStackOp<Variant>::push(L, ret);
			return 1;
		}

		return 0;
	} else {
		LuauVariant self;
		void *self_ptr = nullptr;

		if (p_method.is_static) {
			self_ptr = nullptr;
		} else {
			self.lua_check(L, 1, p_builtin_class.type);
			self_ptr = self.get_opaque_pointer();
		}

		if (p_method.return_type != -1) {
			LuauVariant ret;
			ret.initialize((GDExtensionVariantType)p_method.return_type);

			SET_CALL_STACK(L);
			p_method.func(self_ptr, pargs.ptr(), ret.get_opaque_pointer(), pargs.size());
			CLEAR_CALL_STACK;

			ret.lua_push(L);
			return 1;
		} else {
			SET_CALL_STACK(L);
			p_method.func(self_ptr, pargs.ptr(), nullptr, pargs.size());
			CLEAR_CALL_STACK;
			return 0;
		}
	}
}

static int luaGD_builtin_method(lua_State *L) {
	const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
	const ApiVariantMethod *method = luaGD_lightudataup<ApiVariantMethod>(L, 2);

	return call_builtin_method(L, *builtin_class, *method);
}

static void push_builtin_method(lua_State *L, const ApiBuiltinClass &p_builtin_class, const ApiVariantMethod &p_method) {
	lua_pushlightuserdata(L, (void *)&p_builtin_class);
	lua_pushlightuserdata(L, (void *)&p_method);
	lua_pushcclosure(L, luaGD_builtin_method, p_method.debug_name, 2);
}

static int luaGD_builtin_namecall(lua_State *L) {
	const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

	if (const char *name = lua_namecallatom(L, nullptr)) {
		if (strcmp(name, "Set") == 0) {
			if (builtin_class->type != GDEXTENSION_VARIANT_TYPE_DICTIONARY && !get_array_type_info(builtin_class->type))
				luaGD_readonlyerror(L, builtin_class->name);

			LuauVariant self;
			self.lua_check(L, 1, builtin_class->type);

			Variant key = LuaStackOp<Variant>::check(L, 2);

			if (builtin_class->indexed_setter && key.get_type() == Variant::INT) {
				// Indexed
				LuauVariant val;
				val.lua_check(L, 3, builtin_class->indexing_return_type);

				builtin_class->indexed_setter(self.get_opaque_pointer(), key.operator int64_t(), val.get_opaque_pointer());
				return 0;
			}

			if (builtin_class->keyed_setter) {
				// Keyed
				// if the key or val is ever assumed to not be Variant, this will segfault. nice.
				Variant val = LuaStackOp<Variant>::check(L, 3);
				builtin_class->keyed_setter(self.get_opaque_pointer(), &key, &val);
				return 0;
			}

			luaL_error(L, "class %s does not have any indexed or keyed setter", builtin_class->name);
		}

		if (strcmp(name, "Get") == 0) {
			LuauVariant self;
			self.lua_check(L, 1, builtin_class->type);

			Variant key = LuaStackOp<Variant>::check(L, 2);

			if (builtin_class->indexed_getter && key.get_type() == Variant::INT) {
				// Indexed
				LuauVariant ret;
				ret.initialize(builtin_class->indexing_return_type);

				builtin_class->indexed_getter(self.get_opaque_pointer(), key.operator int64_t(), ret.get_opaque_pointer());

				ret.lua_push(L);
				return 1;
			}

			if (builtin_class->keyed_getter) {
				// Keyed
				Variant self_var = LuaStackOp<Variant>::check(L, 1);

				// misleading types: keyed_checker expects the type pointer, not a variant
				if (builtin_class->keyed_checker(self.get_opaque_pointer(), &key)) {
					Variant ret;
					// this is sketchy. if key or ret is ever assumed by Godot to not be Variant, this will segfault. Cool!
					// ! see: core/variant/variant_setget.cpp
					builtin_class->keyed_getter(self.get_opaque_pointer(), &key, &ret);

					LuaStackOp<Variant>::push(L, ret);
					return 1;
				} else {
					luaL_error(L, "this %s does not have key '%s'", builtin_class->name, key.stringify().utf8().get_data());
				}
			}

			luaL_error(L, "class %s does not have any indexed or keyed getter", builtin_class->name);
		}

		if (builtin_class->methods.has(name))
			return call_builtin_method(L, *builtin_class, builtin_class->methods.get(name));

		luaGD_nomethoderror(L, name, builtin_class->name);
	}

	luaGD_nonamecallatomerror(L);
}

static int luaGD_builtin_operator(lua_State *L) {
	GDExtensionVariantType type = GDExtensionVariantType(lua_tointeger(L, lua_upvalueindex(1)));
	const Vector<ApiVariantOperator> *operators = luaGD_lightudataup<Vector<ApiVariantOperator>>(L, 2);

	LuauVariant self;
	int right_idx = 2;

	if (LuauVariant::lua_is(L, 1, type)) {
		self.lua_check(L, 1, type);
	} else {
		// Need to handle reverse calls of this method to ensure, for example,
		// operators with numbers (which do not have any metamethods) are handled correctly.
		right_idx = 1;
		self.lua_check(L, 2, type);
	}

	LuauVariant right;
	void *right_ptr = nullptr;

	for (const ApiVariantOperator &op : *operators) {
		if (op.right_type == GDEXTENSION_VARIANT_TYPE_NIL) {
			right_ptr = nullptr;
		} else if (LuauVariant::lua_is(L, right_idx, op.right_type)) {
			right.lua_check(L, right_idx, op.right_type);
			right_ptr = right.get_opaque_pointer();
		} else {
			continue;
		}

		LuauVariant ret;
		ret.initialize(op.return_type);

		op.eval(self.get_opaque_pointer(), right_ptr, ret.get_opaque_pointer());

		ret.lua_push(L);
		return 1;
	}

	luaL_error(L, "no operator matched for arguments of type %s and %s", luaL_typename(L, 1), luaL_typename(L, 2));
}

static void luaGD_builtin_unbound(lua_State *L, GDExtensionVariantType p_variant_type, const char *p_type_name, const char *p_metatable_name) {
	luaL_newmetatable(L, p_metatable_name);

	lua_pushstring(L, p_type_name);
	lua_setfield(L, -2, "__type");

	lua_pushcfunction(L, luaGD_variant_tostring, VARIANT_TOSTRING_DEBUG_NAME);
	lua_setfield(L, -2, "__tostring");

	// Variant type ID
	lua_pushinteger(L, p_variant_type);
	lua_setfield(L, -2, MT_VARIANT_TYPE);

	lua_setreadonly(L, -1, true);
	lua_pop(L, 1); // metatable
}

void luaGD_openbuiltins(lua_State *L) {
	LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded");

	const ExtensionApi &extension_api = get_extension_api();

	for (const ApiBuiltinClass &builtin_class : extension_api.builtin_classes) {
		if (builtin_class.type != GDEXTENSION_VARIANT_TYPE_STRING) {
			luaL_newmetatable(L, builtin_class.metatable_name);
			luaGD_initmetatable(L, -1, builtin_class.type, builtin_class.name);

			// Members (__newindex, __index)
			lua_pushstring(L, builtin_class.name);
			lua_pushcclosure(L, luaGD_builtin_newindex, builtin_class.newindex_debug_name, 1);
			lua_setfield(L, -2, "__newindex");

			lua_pushlightuserdata(L, (void *)&builtin_class);
			lua_pushcclosure(L, luaGD_builtin_index, builtin_class.index_debug_name, 1);
			lua_setfield(L, -2, "__index");

			// Methods (__namecall)
			lua_pushlightuserdata(L, (void *)&builtin_class);
			lua_pushcclosure(L, luaGD_builtin_namecall, builtin_class.namecall_debug_name, 1);
			lua_setfield(L, -2, "__namecall");

			// Operators (misc metatable)
			for (const KeyValue<GDExtensionVariantOperator, Vector<ApiVariantOperator>> &pair : builtin_class.operators) {
				lua_pushinteger(L, builtin_class.type);
				lua_pushlightuserdata(L, (void *)&pair.value);
				lua_pushcclosure(L, luaGD_builtin_operator, builtin_class.operator_debug_names[pair.key], 2);

				const char *op_mt_name = nullptr;

				switch (pair.key) {
					case GDEXTENSION_VARIANT_OP_EQUAL:
						op_mt_name = "__eq";
						break;
					case GDEXTENSION_VARIANT_OP_LESS:
						op_mt_name = "__lt";
						break;
					case GDEXTENSION_VARIANT_OP_LESS_EQUAL:
						op_mt_name = "__le";
						break;
					case GDEXTENSION_VARIANT_OP_ADD:
						op_mt_name = "__add";
						break;
					case GDEXTENSION_VARIANT_OP_SUBTRACT:
						op_mt_name = "__sub";
						break;
					case GDEXTENSION_VARIANT_OP_MULTIPLY:
						op_mt_name = "__mul";
						break;
					case GDEXTENSION_VARIANT_OP_DIVIDE:
						op_mt_name = "__div";
						break;
					case GDEXTENSION_VARIANT_OP_MODULE:
						op_mt_name = "__mod";
						break;
					case GDEXTENSION_VARIANT_OP_NEGATE:
						op_mt_name = "__unm";
						break;
					case GDEXTENSION_VARIANT_OP_POWER:
						op_mt_name = "__pow";
						break;

					default:
						ERR_FAIL_MSG("Variant operator not handled");
				}

				lua_setfield(L, -2, op_mt_name);
			}

			// Array type handling
			if (const ArrayTypeInfo *arr_type_info = get_array_type_info(builtin_class.type)) {
				// __len
				lua_pushcfunction(L, arr_type_info->len, arr_type_info->len_debug_name);
				lua_setfield(L, -2, "__len");

				// __iter
				lua_pushcfunction(L, arr_type_info->iter_next, arr_type_info->iter_next_debug_name);
				lua_pushcclosure(L, luaGD_array_iter, BUILTIN_MT_NAME(Array) ".__iter", 1);
				lua_setfield(L, -2, "__iter");
			}

			// Dictionary iteration
			if (builtin_class.type == GDEXTENSION_VARIANT_TYPE_DICTIONARY) {
				lua_pushcfunction(L, luaGD_dict_next, BUILTIN_MT_NAME(Dictionary) ".next");
				lua_pushcclosure(L, luaGD_dict_iter, BUILTIN_MT_NAME(Dictionary) ".__iter", 1);
				lua_setfield(L, -2, "__iter");
			}

			lua_setreadonly(L, -1, true);
			lua_pop(L, 1);
		}

		lua_newtable(L);
		luaGD_initglobaltable(L, -1, builtin_class.name);

		// Enums
		for (const ApiEnum &class_enum : builtin_class.enums) {
			push_enum(L, class_enum);
			lua_setfield(L, -2, class_enum.name);
		}

		// Constants
		for (const ApiVariantConstant &constant : builtin_class.constants) {
			LuaStackOp<Variant>::push(L, constant.value);
			lua_setfield(L, -2, constant.name);
		}

		// Constructors (global .new)
		if (builtin_class.type == GDEXTENSION_VARIANT_TYPE_CALLABLE) { // Special case for Callable security
			lua_pushcfunction(L, luaGD_callable_ctor, "Callable.new");
			lua_setfield(L, -2, "new");
		} else if (builtin_class.type != GDEXTENSION_VARIANT_TYPE_STRING) {
			lua_pushlightuserdata(L, (void *)&builtin_class);
			lua_pushstring(L, builtin_class.constructor_error_string);
			lua_pushcclosure(L, luaGD_builtin_ctor, builtin_class.constructor_debug_name, 2);
			lua_setfield(L, -2, "new");
		}

		// Static methods
		if (strcmp(builtin_class.name, "String") == 0) {
			for (const KeyValue<String, ApiVariantMethod> &pair : builtin_class.methods) {
				push_builtin_method(L, builtin_class, pair.value);
				lua_setfield(L, -2, pair.value.name);
			}
		}

		for (const ApiVariantMethod &static_method : builtin_class.static_methods) {
			push_builtin_method(L, builtin_class, static_method);
			lua_setfield(L, -2, static_method.name);
		}

		lua_pop(L, 1);
	}

	// Special cases
	luaGD_builtin_unbound(L, GDEXTENSION_VARIANT_TYPE_STRING_NAME, "StringName", BUILTIN_MT_NAME(StringName));
	luaGD_builtin_unbound(L, GDEXTENSION_VARIANT_TYPE_NODE_PATH, "NodePath", BUILTIN_MT_NAME(NodePath));
}
