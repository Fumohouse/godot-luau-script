#include "core/godot_bindings.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/ref_counted.hpp>

#include "core/extension_api.h"
#include "core/lua_utils.h"
#include "core/permissions.h"
#include "core/stack.h"
#include "scripting/luau_script.h"
#include "services/sandbox_service.h"
#include "utils/utils.h"
#include "utils/wrapped_no_binding.h"

using namespace godot;

static int luaGD_class_ctor(lua_State *L) {
	StringName class_name = lua_tostring(L, lua_upvalueindex(1));

	GDExtensionObjectPtr obj = internal::gdextension_interface_classdb_construct_object2(&class_name);
	nb::Object(obj).notification(Object::NOTIFICATION_POSTINITIALIZE);
	LuaStackOp<Object *>::push(L, obj);
	return 1;
}

#define LUAGD_CLASS_METAMETHOD                                     \
	int class_idx = lua_tointeger(L, lua_upvalueindex(1));         \
	const Vector<ApiClass> &classes = get_extension_api().classes; \
	const ApiClass *current_class = &classes[class_idx];           \
                                                                   \
	GDExtensionObjectPtr self = LuaStackOp<Object *>::check(L, 1); \
	if (!self)                                                     \
		luaGD_objnullerror(L, 1);

#define INHERIT_OR_BREAK                                     \
	if (current_class->parent_idx >= 0)                      \
		current_class = &classes[current_class->parent_idx]; \
	else                                                     \
		break;

static void handle_object_returned(GDExtensionObjectPtr p_obj) {
	// if Godot returns a RefCounted from a method, it is always in the form of a Ref.
	// as such, the RefCounted we receive will be initialized at a refcount of 1
	// and is considered "initialized" (first Ref already made).
	// we need to decrement the refcount by 1 after pushing to Luau to avoid leak.
	if (GDExtensionObjectPtr rc = Utils::cast_obj<RefCounted>(p_obj))
		nb::RefCounted(rc).unreference();
}

static int call_class_method(lua_State *L, const ApiClass &p_class, const ApiClassMethod &p_method) {
	if (!p_method.bind)
		luaL_error(L, "method %s::%s is not present in this Godot build", p_class.name, p_method.name);

	luaGD_checkpermissions(L, p_method.debug_name, get_method_permissions(p_class, p_method));

	LocalVector<Variant> varargs;
	LocalVector<LuauVariant> args;
	LocalVector<const void *> pargs;

	GDExtensionObjectPtr self = nullptr;

	if (!p_method.is_static) {
		LuauVariant self_var;
		self_var.lua_check(L, 1, GDEXTENSION_VARIANT_TYPE_OBJECT, p_class.name);

		self = *self_var.get_ptr<GDExtensionObjectPtr>();

		if (!p_method.is_const && SandboxService::get_singleton()) {
			const BitField<ThreadPermissions> *permissions = SandboxService::get_singleton()->get_object_permissions(self);
			if (permissions)
				luaGD_checkpermissions(L, (nb::Object(self).to_string() + "." + p_method.name).utf8().get_data(), *permissions);
		}
	}

	get_arguments<ApiClassMethod, ApiClassArgument>(L, p_method.name, &varargs, &args, &pargs, p_method);

	if (p_method.is_vararg) {
		Variant ret;
		GDExtensionCallError error;

		SET_CALL_STACK(L);
		internal::gdextension_interface_object_method_bind_call(p_method.bind, self, pargs.ptr(), pargs.size(), &ret, &error);
		CLEAR_CALL_STACK;

		if (p_method.return_type.type != -1) {
			LuaStackOp<Variant>::push(L, ret);

			if (ret.get_type() == Variant::OBJECT) {
				Object *obj = ret.operator Object *();
				handle_object_returned(obj ? obj->_owner : nullptr);
			}

			return 1;
		}

		return 0;
	} else {
		LuauVariant ret;
		void *ret_ptr = nullptr;

		if (p_method.return_type.type != -1) {
			ret.initialize((GDExtensionVariantType)p_method.return_type.type);
			ret_ptr = ret.get_opaque_pointer();
		}

		SET_CALL_STACK(L);
		internal::gdextension_interface_object_method_bind_ptrcall(p_method.bind, self, pargs.ptr(), ret_ptr);
		CLEAR_CALL_STACK;

		if (ret.get_type() != -1) {
			ret.lua_push(L);

			// handle ref returned from Godot
			if (ret.get_type() == GDEXTENSION_VARIANT_TYPE_OBJECT && *ret.get_ptr<GDExtensionObjectPtr>())
				handle_object_returned(*ret.get_ptr<GDExtensionObjectPtr>());

			return 1;
		}

		return 0;
	}
}

static int luaGD_class_method(lua_State *L) {
	const ApiClass *g_class = luaGD_lightudataup<ApiClass>(L, 1);
	const ApiClassMethod *method = luaGD_lightudataup<ApiClassMethod>(L, 2);

	return call_class_method(L, *g_class, *method);
}

static void push_class_method(lua_State *L, const ApiClass &p_class, const ApiClassMethod &p_method) {
	lua_pushlightuserdata(L, (void *)&p_class);
	lua_pushlightuserdata(L, (void *)&p_method);
	lua_pushcclosure(L, luaGD_class_method, p_method.debug_name, 2);
}

#define FREE_NAME "Free"
#define FREE_DBG_NAME "Godot.Object.Object.Free"
static int luaGD_class_free(lua_State *L) {
	GDExtensionObjectPtr self = LuaStackOp<Object *>::check(L, 1);
	if (!self)
		luaGD_objnullerror(L, 1);

	if (nb::Object(self).is_class(RefCounted::get_class_static()))
		luaL_error(L, "cannot free a RefCounted object");

	// Zero out the object to prevent segfaults
	*LuaStackOp<Object *>::get_id(L, 1) = 0;

	internal::gdextension_interface_object_destroy(self);
	return 0;
}

#define ISA_NAME "IsA"
#define ISA_DBG_NAME "Godot.Object.Object.IsA"
static int luaGD_class_isa(lua_State *L) {
	GDExtensionObjectPtr self = LuaStackOp<Object *>::check(L, 1);

	if (!self) {
		lua_pushboolean(L, false);
		return 1;
	}

	String type;
	LuauScript *script = nullptr;
	luascript_get_classdef_or_type(L, 2, type, script);

	nb::Object self_obj = self;

	if (!type.is_empty()) {
		lua_pushboolean(L, self_obj.is_class(type));
		return 1;
	} else {
		Ref<LuauScript> s = self_obj.get_script();

		while (s.is_valid()) {
			if (s == script) {
				lua_pushboolean(L, true);
				return 1;
			}

			s = s->get_base();
		}

		lua_pushboolean(L, false);
		return 1;
	}
}

#define SET_NAME "Set"
#define SET_DBG_NAME "Godot.Object.Object.Set"
static int luaGD_class_set(lua_State *L) {
	GDExtensionObjectPtr self = LuaStackOp<Object *>::check(L, 1);
	if (!self)
		luaGD_objnullerror(L, 1);

	StringName property = LuaStackOp<StringName>::check(L, 2);
	Variant value = LuaStackOp<Variant>::check(L, 3);

	// Properties with a / are generally per-instance (e.g. for AnimationTree)
	// and for now it's probably safe to assume these can be set with BASE permissions.
	if (!property.contains("/")) {
		luaGD_checkpermissions(L, SET_DBG_NAME, PERMISSION_INTERNAL);
	}

	nb::Object(self).set(property, value);
	return 0;
}

#define GET_NAME "Get"
#define GET_DBG_NAME "Godot.Object.Object.Get"
static int luaGD_class_get(lua_State *L) {
	GDExtensionObjectPtr self = LuaStackOp<Object *>::check(L, 1);
	if (!self)
		luaGD_objnullerror(L, 1);

	StringName property = LuaStackOp<StringName>::check(L, 2);

	if (!property.contains("/")) {
		luaGD_checkpermissions(L, GET_DBG_NAME, PERMISSION_INTERNAL);
	}

	LuaStackOp<Variant>::push(L, nb::Object(self).get(property));
	return 1;
}

static int luaGD_class_namecall(lua_State *L) {
	LUAGD_CLASS_METAMETHOD

	const char *class_name = current_class->name;

	if (const char *name = lua_namecallatom(L, nullptr)) {
		if (strcmp(name, FREE_NAME) == 0) {
			return luaGD_class_free(L);
		} else if (strcmp(name, ISA_NAME) == 0) {
			return luaGD_class_isa(L);
		} else if (strcmp(name, SET_NAME) == 0) {
			return luaGD_class_set(L);
		} else if (strcmp(name, GET_NAME) == 0) {
			return luaGD_class_get(L);
		}

		while (true) {
			if (current_class->methods.has(name))
				return call_class_method(L, *current_class, current_class->methods[name]);

			INHERIT_OR_BREAK
		}

		luaGD_nomethoderror(L, name, class_name);
	}

	luaGD_nonamecallatomerror(L);
}

static int call_property_setget(lua_State *L, int p_class_idx, const ApiClassProperty &p_property, const String &p_method) {
	if (p_property.index != -1) {
		lua_pushinteger(L, p_property.index);
		lua_insert(L, 2);
	}

	const Vector<ApiClass> &classes = get_extension_api().classes;

	while (p_class_idx != -1) {
		const ApiClass &current_class = classes[p_class_idx];

		HashMap<String, ApiClassMethod>::ConstIterator E = current_class.methods.find(p_method);

		if (E) {
			return call_class_method(L, current_class, E->value);
		}

		p_class_idx = current_class.parent_idx;
	}

	luaL_error(L, "setter/getter '%s' was not found", p_method.utf8().get_data());
}

struct CrossVMMethod {
	LuauScriptInstance *inst;
	const GDMethod *method;
};

STACK_OP_PTR_DEF(CrossVMMethod)
UDATA_STACK_OP_IMPL(CrossVMMethod, "Luau.CrossVMMethod", DTOR(CrossVMMethod))

static int luaGD_crossvm_call(lua_State *L) {
	CrossVMMethod m = LuaStackOp<CrossVMMethod>::check(L, 1);
	lua_remove(L, 1); // To get args

	LocalVector<Variant> varargs;
	LocalVector<const void *> pargs;

	get_arguments<GDMethod, GDProperty>(L, m.method->name.utf8().get_data(), &varargs, nullptr, &pargs, *m.method);

	Variant ret;
	GDExtensionCallError err;
	m.inst->call(m.method->name, reinterpret_cast<const Variant *const *>(pargs.ptr()), pargs.size(), &ret, &err);

	// Error should have been sent out when getting arguments, so ignore it.

	LuaStackOp<Variant>::push(L, ret);
	return 1;
}

static int luaGD_class_index(lua_State *L) {
	LUAGD_CLASS_METAMETHOD

	const char *class_name = current_class->name;
	const char *key = luaL_checkstring(L, 2);

	LuauScriptInstance *inst = LuauScriptInstance::from_object(self);

	if (inst) {
		GDThreadData *udata = luaGD_getthreaddata(L);

		// Definition table
		if (inst->get_vm_type() == udata->vm_type) {
			LuauScript *s = inst->get_script().ptr();

			while (s) {
				lua_pushstring(L, key);
				s->def_table_get(L);

				if (!lua_isnil(L, -1))
					return 1;

				lua_pop(L, 1); // value

				s = s->get_base().ptr();
			}
		}

		if (const GDMethod *method = inst->get_method(key)) {
			LuaStackOp<CrossVMMethod>::push(L, { inst, method });
			return 1;
		} else if (const GDClassProperty *prop = inst->get_property(key)) {
			if (prop->setter != StringName() && prop->getter == StringName())
				luaGD_propwriteonlyerror(L, key);

			Variant ret;
			LuauScriptInstance::PropertySetGetError err = LuauScriptInstance::PROP_OK;
			bool is_valid = inst->get(key, ret, &err);

			if (is_valid) {
				LuaStackOp<Variant>::push(L, ret);
				return 1;
			} else if (err == LuauScriptInstance::PROP_GET_FAILED) {
				luaL_error(L, "failed to get property '%s'; see previous errors for more information", key);
			} else {
				luaL_error(L, "failed to get property '%s': unknown error", key); // due to the checks above, this should hopefully never happen
			}
		} else if (const GDMethod *signal = inst->get_signal(key)) {
			nb::Object self_obj = self;
			LuaStackOp<Signal>::push(L, Signal(&self_obj, key));
			return 1;
		} else if (const Variant *constant = inst->get_constant(key)) {
			LuaStackOp<Variant>::push(L, *constant);
			return 1;
		}
	}

	if (strcmp(key, FREE_NAME) == 0) {
		lua_pushcfunction(L, luaGD_class_free, FREE_DBG_NAME);
		return 1;
	} else if (strcmp(key, ISA_NAME) == 0) {
		lua_pushcfunction(L, luaGD_class_isa, ISA_DBG_NAME);
		return 1;
	} else if (strcmp(key, SET_NAME) == 0) {
		lua_pushcfunction(L, luaGD_class_set, SET_DBG_NAME);
		return 1;
	} else if (strcmp(key, GET_NAME) == 0) {
		lua_pushcfunction(L, luaGD_class_get, GET_DBG_NAME);
		return 1;
	}

	while (true) {
		HashMap<String, ApiClassMethod>::ConstIterator E = current_class->methods.find(key);

		if (E) {
			push_class_method(L, *current_class, E->value);
			return 1;
		}

		HashMap<String, ApiClassProperty>::ConstIterator F = current_class->properties.find(key);

		if (F) {
			lua_remove(L, 2); // key

			if (F->value.getter == "")
				luaGD_propwriteonlyerror(L, key);

			return call_property_setget(L, class_idx, F->value, F->value.getter);
		}

		HashMap<String, ApiClassSignal>::ConstIterator G = current_class->signals.find(key);

		if (G) {
			nb::Object self_obj = self;
			LuaStackOp<Signal>::push(L, Signal(&self_obj, G->value.gd_name));
			return 1;
		}

		INHERIT_OR_BREAK
	}

	// Attempt get on internal table.
	// the key is already on the top of the stack
	if (inst && inst->table_get(L))
		return 1;

	luaGD_indexerror(L, key, class_name);
}

static int luaGD_class_newindex(lua_State *L) {
	LUAGD_CLASS_METAMETHOD

	const char *class_name = current_class->name;
	const char *key = luaL_checkstring(L, 2);

	LuauScriptInstance *inst = LuauScriptInstance::from_object(self);

	if (inst) {
		if (const GDClassProperty *prop = inst->get_property(key)) {
			if (prop->getter != StringName() && prop->setter == StringName())
				luaGD_propreadonlyerror(L, key);

			LuauScriptInstance::PropertySetGetError err = LuauScriptInstance::PROP_OK;
			LuauVariant val;
			val.lua_check(L, 3, prop->property.type);

			bool is_valid = inst->set(key, val.to_variant(), &err);

			if (is_valid) {
				return 0;
			} else if (err == LuauScriptInstance::PROP_WRONG_TYPE) {
				luaGD_valueerror(L, key,
						luaL_typename(L, 3),
						Variant::get_type_name((Variant::Type)prop->property.type).utf8().get_data());
			} else if (err == LuauScriptInstance::PROP_SET_FAILED) {
				luaL_error(L, "failed to set property '%s'; see previous errors for more information", key);
			} else {
				luaL_error(L, "failed to set property '%s': unknown error", key); // should never happen
			}
		} else if (inst->get_signal(key)) {
			luaL_error(L, "cannot assign to signal '%s'", key);
		} else if (inst->get_constant(key)) {
			luaL_error(L, "cannot assign to constant '%s'", key);
		}
	}

	while (true) {
		HashMap<String, ApiClassProperty>::ConstIterator E = current_class->properties.find(key);

		if (E) {
			lua_remove(L, 2); // key

			if (E->value.setter == "")
				luaGD_propreadonlyerror(L, key);

			return call_property_setget(L, class_idx, E->value, E->value.setter);
		}

		if (current_class->signals.has(key))
			luaL_error(L, "cannot assign to signal '%s'", key);

		INHERIT_OR_BREAK
	}

	// Attempt set on internal table.
	// key and value are already on the top of the stack
	if (inst && inst->table_set(L))
		return 0;

	luaGD_indexerror(L, key, class_name);
}

void luaGD_openclasses(lua_State *L) {
	LUAGD_LOAD_GUARD(L, "_gdClassesLoaded");

	const ExtensionApi &extension_api = get_extension_api();
	const ApiClass *classes = extension_api.classes.ptr();

	for (int i = 0; i < extension_api.classes.size(); i++) {
		const ApiClass &g_class = classes[i];

		luaL_newmetatable(L, g_class.metatable_name);
		luaGD_initmetatable(L, -1, GDEXTENSION_VARIANT_TYPE_OBJECT, g_class.name);

		// Object type ID
		lua_pushinteger(L, i);
		lua_setfield(L, -2, MT_CLASS_TYPE);

		// Properties (__newindex, __index)
		lua_pushinteger(L, i);
		lua_pushcclosure(L, luaGD_class_newindex, g_class.newindex_debug_name, 1);
		lua_setfield(L, -2, "__newindex");

		lua_pushinteger(L, i);
		lua_pushcclosure(L, luaGD_class_index, g_class.index_debug_name, 1);
		lua_setfield(L, -2, "__index");

		lua_setreadonly(L, -1, true);

		String namecall_mt_name = String(g_class.metatable_name) + ".Namecall";
		luaL_newmetatable(L, namecall_mt_name.utf8().get_data());

		// Methods (__namecall)
		lua_pushinteger(L, i);
		lua_pushcclosure(L, luaGD_class_namecall, g_class.namecall_debug_name, 1);
		lua_setfield(L, -2, "__namecall");

		// Copy main metatable to namecall metatable
		lua_pushnil(L);
		while (lua_next(L, -3) != 0) {
			lua_pushvalue(L, -2);
			lua_insert(L, -2);
			lua_settable(L, -4);
		}

		lua_setreadonly(L, -1, true);
		lua_pop(L, 2); // both metatables

		lua_newtable(L);
		luaGD_initglobaltable(L, -1, g_class.name);

		// Enums
		for (const ApiEnum &class_enum : g_class.enums) {
			push_enum(L, class_enum);
			lua_setfield(L, -2, class_enum.name);
		}

		// Constants
		for (const ApiConstant &constant : g_class.constants) {
			lua_pushinteger(L, constant.value);
			lua_setfield(L, -2, constant.name);
		}

		// Constructor (global .new)
		if (g_class.is_instantiable) {
			lua_pushstring(L, g_class.name);
			lua_pushcclosure(L, luaGD_class_ctor, g_class.constructor_debug_name, 1);
			lua_setfield(L, -2, "new");
		}

		// Singleton
		if (g_class.singleton) {
			LuaStackOp<Object *>::push(L, g_class.singleton);
			lua_setfield(L, -2, "singleton");
		}

		// Static methods
		for (const ApiClassMethod &static_method : g_class.static_methods) {
			push_class_method(L, g_class, static_method);
			lua_setfield(L, -2, static_method.name);
		}

		// Overridden Object methods
		if (strcmp(g_class.name, "Object") == 0) {
#define CUSTOM_OBJ_METHOD(m_method, m_name, m_dbg_name) \
	lua_pushcfunction(L, m_method, m_dbg_name);         \
	lua_setfield(L, -2, m_name);
			CUSTOM_OBJ_METHOD(luaGD_class_free, FREE_NAME, FREE_DBG_NAME)
			CUSTOM_OBJ_METHOD(luaGD_class_isa, ISA_NAME, ISA_DBG_NAME)
			CUSTOM_OBJ_METHOD(luaGD_class_set, SET_NAME, SET_DBG_NAME)
			CUSTOM_OBJ_METHOD(luaGD_class_get, GET_NAME, GET_DBG_NAME)
		}

		lua_pop(L, 1);
	}

	// CrossVMMethod
	{
		luaL_newmetatable(L, "Luau.CrossVMMethod");

		lua_pushcfunction(L, luaGD_crossvm_call, "Luau.CrossVMMethod.__call");
		lua_setfield(L, -2, "__call");

		lua_setreadonly(L, -1, true);
		lua_pop(L, 1);
	}
}
