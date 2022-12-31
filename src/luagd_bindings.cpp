#include "luagd_bindings.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <type_traits>

#include "extension_api.h"
#include "luagd_bindings_stack.gen.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
#include "luagd_variant.h"
#include "luau_script.h"

namespace godot {
class Object;
}

/////////////
// Generic //
/////////////

static bool variant_types_compatible(Variant::Type t1, Variant::Type t2) {
    return t1 == t2 ||
            (t1 == Variant::FLOAT && t2 == Variant::INT) ||
            (t1 == Variant::INT && t2 == Variant::FLOAT);
}

static void check_variant(lua_State *L, int idx, const Variant &val, GDExtensionVariantType p_expected_type, String type_name = "") {
    Variant::Type expected_type = (Variant::Type)p_expected_type;

    if (expected_type != Variant::NIL && !variant_types_compatible(val.get_type(), expected_type))
        luaL_typeerrorL(L, idx, Variant::get_type_name(expected_type).utf8().get_data());
    else if (expected_type == Variant::OBJECT) {
        Object *obj = val;
        if (!obj->is_class(type_name))
            luaL_typeerrorL(L, idx, type_name.utf8().get_data());
    }
}

static void push_enum(lua_State *L, const ApiEnum &p_enum) // notation cause reserved keyword
{
    lua_createtable(L, 0, p_enum.values.size());

    for (const Pair<String, int32_t> &value : p_enum.values) {
        lua_pushinteger(L, value.second);
        lua_setfield(L, -2, value.first.utf8().get_data());
    }

    lua_setreadonly(L, -1, true);
}

/* GETTING ARGUMENTS */

// From stack
template <typename T>
_FORCE_INLINE_ static void get_argument(lua_State *L, int idx, const T &arg, LuauVariant &out) {
    out.lua_check(L, idx, arg.type);
}

template <>
_FORCE_INLINE_ void get_argument<ApiClassArgument>(lua_State *L, int idx, const ApiClassArgument &arg, LuauVariant &out) {
    const ApiClassType &type = arg.type;

    if (type.is_typed_array) {
        // TODO: type check the array (can be Variant or class type)
        out.lua_check(L, idx, GDEXTENSION_VARIANT_TYPE_ARRAY);
    } else {
        out.lua_check(L, idx, (GDExtensionVariantType)type.type, type.type_name);
    }
}

// Defaults
template <typename T>
struct has_default_value_trait : std::true_type {};

template <>
struct has_default_value_trait<ApiArgumentNoDefault> : std::false_type {};

template <typename TMethod, typename TArg>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int arg_offset, int nargs, Vector<const void *> &pargs, const TMethod &method, std::true_type const &) {
    for (int i = nargs; i < method.arguments.size(); i++) {
        const TArg &arg = method.arguments[i];

        if (arg.has_default_value) {
            pargs.set(i, arg.default_value.get_opaque_pointer());
        } else {
            LuauVariant dummy;
            get_argument(L, i + 1 + arg_offset, arg, dummy);
        }
    }
}

template <typename TMethod, typename>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int arg_offset, int nargs, Vector<const void *> &pargs, const TMethod &method, std::false_type const &) {
    LuauVariant dummy;
    get_argument(L, nargs + 1 + arg_offset, method.arguments[nargs], dummy);
}

// Getters for argument types
template <typename T>
_FORCE_INLINE_ static GDExtensionVariantType get_arg_type(const T &arg) { return arg.type; }

template <>
_FORCE_INLINE_ GDExtensionVariantType get_arg_type<ApiClassArgument>(const ApiClassArgument &arg) { return (GDExtensionVariantType)arg.type.type; }

template <typename T>
_FORCE_INLINE_ static const String &get_arg_type_name(const T &arg) {
    static String s;
    return s;
}

template <>
_FORCE_INLINE_ const String &get_arg_type_name<ApiClassArgument>(const ApiClassArgument &arg) { return arg.type.type_name; }

// Getters for method types
template <typename T>
_FORCE_INLINE_ static bool is_method_static(const T &method) { return method.is_static; }

template <>
_FORCE_INLINE_ bool is_method_static<ApiUtilityFunction>(const ApiUtilityFunction &method) { return true; }

template <typename T>
_FORCE_INLINE_ static bool is_method_vararg(const T &method) { return method.is_vararg; }

// this is magic
template <typename T, typename TArg>
static int get_arguments(lua_State *L,
        Vector<Variant> &varargs,
        Vector<LuauVariant> &args,
        Vector<const void *> &pargs,
        const T &method) {
    // arg 1 is self for instance methods
    int arg_offset = is_method_static(method) ? 0 : 1;
    int nargs = lua_gettop(L) - arg_offset;

    if (method.arguments.size() > nargs)
        pargs.resize(method.arguments.size());
    else
        pargs.resize(nargs);

    if (is_method_vararg(method)) {
        varargs.resize(nargs);

        for (int i = 0; i < nargs; i++) {
            Variant arg = LuaStackOp<Variant>::check(L, i + 1 + arg_offset);
            if (i < method.arguments.size())
                check_variant(L, i + 1 + arg_offset, arg, get_arg_type(method.arguments[i]), get_arg_type_name(method.arguments[i]));

            varargs.set(i, arg);
            pargs.set(i, &varargs[i]);
        }
    } else {
        args.resize(nargs);

        if (nargs > method.arguments.size())
            luaL_error(L, "too many arguments to '%s' (expected at most %d)", method.name.utf8().get_data(), method.arguments.size());

        for (int i = 0; i < nargs; i++) {
            get_argument(L, i + 1 + arg_offset, method.arguments[i], args.ptrw()[i]);
            pargs.set(i, args[i].get_opaque_pointer());
        }
    }

    if (nargs < method.arguments.size())
        get_default_args<T, TArg>(L, arg_offset, nargs, pargs, method, has_default_value_trait<TArg>());

    return nargs;
}

//////////////
// Builtins //
//////////////

static int luaGD_builtin_ctor(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    const char *error_string = lua_tostring(L, lua_upvalueindex(2));

    // remove first argument to __call (global table)
    lua_remove(L, 1);

    int nargs = lua_gettop(L);

    Vector<LuauVariant> args;
    args.resize(nargs);

    Vector<const void *> pargs;
    pargs.resize(nargs);

    for (const ApiVariantConstructor &ctor : builtin_class->constructors) {
        if (nargs != ctor.arguments.size())
            continue;

        bool valid = true;

        for (int i = 0; i < nargs; i++) {
            GDExtensionVariantType type = ctor.arguments[i].type;

            if (!LuaStackOp<Variant>::is(L, i + 1) ||
                    !variant_types_compatible(LuaStackOp<Variant>::get(L, i + 1).get_type(), (Variant::Type)type)) {
                valid = false;
                break;
            }

            LuauVariant arg;
            arg.lua_check(L, i + 1, type);

            args.set(i, arg);
            pargs.set(i, args[i].get_opaque_pointer());
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

static int luaGD_builtin_newindex(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    Variant key = LuaStackOp<Variant>::check(L, 2);

    if (builtin_class->indexed_setter != nullptr && key.get_type() == Variant::INT) {
        // Indexed
        LuauVariant val;
        val.lua_check(L, 3, builtin_class->indexing_return_type);

        // lua is 1 indexed :))))
        builtin_class->indexed_setter(self.get_opaque_pointer(), key.operator int64_t() - 1, val.get_opaque_pointer());
        return 0;
    } else if (builtin_class->keyed_setter != nullptr) {
        // Keyed
        // if the key or val is ever assumed to not be Variant, this will segfault. nice.
        Variant val = LuaStackOp<Variant>::check(L, 3);
        builtin_class->keyed_setter(self.get_opaque_pointer(), &key, &val);
        return 0;
    }

    // All other set operations are invalid
    if (builtin_class->members.has(key.operator String()))
        luaL_error(L, "type '%s' is read-only", builtin_class->name.utf8().get_data());
    else
        luaL_error(L, "%s is not a valid member of '%s'",
                key.operator String().utf8().get_data(),
                builtin_class->name.utf8().get_data());
}

static int luaGD_builtin_index(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    Variant key = LuaStackOp<Variant>::check(L, 2);

    if (builtin_class->indexed_getter != nullptr && key.get_type() == Variant::INT) {
        // Indexed
        LuauVariant ret;
        ret.initialize(builtin_class->indexing_return_type);

        // lua is 1 indexed :))))
        builtin_class->indexed_getter(self.get_opaque_pointer(), key.operator int64_t() - 1, ret.get_opaque_pointer());

        ret.lua_push(L);
        return 1;
    } else if (key.get_type() == Variant::STRING) {
        // Members
        String name = key.operator String();

        if (builtin_class->members.has(name)) {
            const ApiVariantMember &member = builtin_class->members.get(name);

            LuauVariant ret;
            ret.initialize(member.type);

            member.getter(self.get_opaque_pointer(), ret.get_opaque_pointer());

            ret.lua_push(L);
            return 1;
        }
    }

    // Keyed
    if (builtin_class->keyed_getter != nullptr) {
        Variant self_var = LuaStackOp<Variant>::check(L, 1);

        // misleading types: keyed_checker expects the type pointer, not a variant
        if (builtin_class->keyed_checker(self.get_opaque_pointer(), &key)) {
            Variant ret;
            // this is sketchy. if key or ret is ever assumed by Godot to not be Variant, this will segfault. Cool!
            // ! see: core/variant/variant_setget.cpp
            builtin_class->keyed_getter(self.get_opaque_pointer(), &key, &ret);

            LuaStackOp<Variant>::push(L, ret);
            return 1;
        }
    }

    luaL_error(L, "%s is not a valid member of '%s'",
            key.operator String().utf8().get_data(),
            builtin_class->name.utf8().get_data());
}

static int call_builtin_method(lua_State *L, const ApiBuiltinClass &builtin_class, const ApiVariantMethod &method) {
    Vector<Variant> varargs;
    Vector<LuauVariant> args;
    Vector<const void *> pargs;

    get_arguments<ApiVariantMethod, ApiArgument>(L, varargs, args, pargs, method);

    if (method.is_vararg) {
        Variant ret;

        if (method.is_static) {
            internal::gde_interface->variant_call_static(builtin_class.type, &method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);
        } else {
            Variant self = LuaStackOp<Variant>::check(L, 1);
            internal::gde_interface->variant_call(&self, &method.gd_name, pargs.ptr(), pargs.size(), &ret, nullptr);

            // HACK: since the value in self is copied,
            // it's necessary to manually assign the changed value back to Luau
            if (!method.is_const) {
                LuauVariant lua_self;
                lua_self.lua_check(L, 1, builtin_class.type);
                lua_self.assign_variant(self);
            }
        }

        if (method.return_type != GDEXTENSION_VARIANT_TYPE_NIL) {
            LuaStackOp<Variant>::push(L, ret);
            return 1;
        }

        return 0;
    } else {
        LuauVariant self;
        void *self_ptr;

        if (method.is_static) {
            self_ptr = nullptr;
        } else {
            self.lua_check(L, 1, builtin_class.type);
            self_ptr = self.get_opaque_pointer();
        }

        if (method.return_type != -1) {
            LuauVariant ret;
            ret.initialize((GDExtensionVariantType)method.return_type);

            method.func(self_ptr, pargs.ptr(), ret.get_opaque_pointer(), pargs.size());

            ret.lua_push(L);
            return 1;
        } else {
            method.func(self_ptr, pargs.ptr(), nullptr, pargs.size());
            return 0;
        }
    }
}

static int luaGD_builtin_method(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    const ApiVariantMethod *method = luaGD_lightudataup<ApiVariantMethod>(L, 2);

    return call_builtin_method(L, *builtin_class, *method);
}

static void push_builtin_method(lua_State *L, const ApiBuiltinClass &builtin_class, const ApiVariantMethod &method) {
    lua_pushlightuserdata(L, (void *)&builtin_class);
    lua_pushlightuserdata(L, (void *)&method);
    lua_pushcclosure(L, luaGD_builtin_method, method.debug_name, 2);
}

static int luaGD_builtin_namecall(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);

    if (const char *name = lua_namecallatom(L, nullptr)) {
        if (builtin_class->methods.has(name))
            return call_builtin_method(L, *builtin_class, builtin_class->methods.get(name));

        luaL_error(L, "'%s' is not a valid method of '%s'", name, builtin_class->name.utf8().get_data());
    }

    luaL_error(L, "no namecallatom");
}

static int luaGD_builtin_operator(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    GDExtensionVariantOperator var_op = (GDExtensionVariantOperator)lua_tointeger(L, lua_upvalueindex(2));

    LuauVariant self;
    self.lua_check(L, 1, builtin_class->type);

    LuauVariant right;
    void *right_ptr;

    for (const ApiVariantOperator &op : builtin_class->operators.get(var_op)) {
        if (op.right_type == GDEXTENSION_VARIANT_TYPE_NIL) {
            right_ptr = nullptr;
        } else if (LuaStackOp<Variant>::is(L, 2) && variant_types_compatible(LuaStackOp<Variant>::get(L, 2).get_type(), (Variant::Type)op.right_type)) {
            right.lua_check(L, 2, op.right_type);
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

    // TODO: say something better here
    luaL_error(L, "no operator matched");
}

void luaGD_openbuiltins(lua_State *L) {
    LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded");

    const ExtensionApi &extension_api = get_extension_api();

    for (const ApiBuiltinClass &builtin_class : extension_api.builtin_classes) {
        luaGD_newlib(L, builtin_class.name.utf8().get_data(), builtin_class.metatable_name);

        // Enums
        for (const ApiEnum &class_enum : builtin_class.enums) {
            push_enum(L, class_enum);
            lua_setfield(L, -3, class_enum.name);
        }

        // Constants
        for (const ApiVariantConstant &constant : builtin_class.constants) {
            LuaStackOp<Variant>::push(L, constant.value);
            lua_setfield(L, -3, constant.name.utf8().get_data());
        }

        // Constructors (global __call)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushstring(L, builtin_class.constructor_error_string);
        lua_pushcclosure(L, luaGD_builtin_ctor, builtin_class.constructor_debug_name, 2);
        lua_setfield(L, -2, "__call");

        // Members (__newindex, __index)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_newindex, builtin_class.newindex_debug_name, 1);
        lua_setfield(L, -4, "__newindex");

        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_index, builtin_class.index_debug_name, 1);
        lua_setfield(L, -4, "__index");

        // Methods (__namecall)
        lua_pushlightuserdata(L, (void *)&builtin_class);
        lua_pushcclosure(L, luaGD_builtin_namecall, builtin_class.namecall_debug_name, 1);
        lua_setfield(L, -4, "__namecall");

        // All methods (global table)
        for (const KeyValue<String, ApiVariantMethod> &pair : builtin_class.methods) {
            push_builtin_method(L, builtin_class, pair.value);
            lua_setfield(L, -3, pair.value.name.utf8().get_data());
        }

        for (const ApiVariantMethod &static_method : builtin_class.static_methods) {
            push_builtin_method(L, builtin_class, static_method);
            lua_setfield(L, -3, static_method.name.utf8().get_data());
        }

        // Operators (misc metatable)
        for (const KeyValue<GDExtensionVariantOperator, Vector<ApiVariantOperator>> &pair : builtin_class.operators) {
            lua_pushlightuserdata(L, (void *)&builtin_class);
            lua_pushinteger(L, pair.key);
            lua_pushcclosure(L, luaGD_builtin_operator, builtin_class.operator_debug_names[pair.key], 2);

            const char *op_mt_name;
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

                // Special (non-Godot) operators
                case GDEXTENSION_VARIANT_OP_MAX:
                    op_mt_name = "__len";
                    break;

                default:
                    ERR_FAIL_MSG("variant operator not handled");
            }

            lua_setfield(L, -4, op_mt_name);
        }

        luaGD_poplib(L, false);
    }
}

/////////////
// Classes //
/////////////

static int luaGD_class_ctor(lua_State *L) {
    StringName class_name = lua_tostring(L, lua_upvalueindex(1));

    GDExtensionObjectPtr native_ptr = internal::gde_interface->classdb_construct_object(&class_name);
    GDObjectInstanceID id = internal::gde_interface->object_get_instance_id(native_ptr);

    Object *obj = ObjectDB::get_instance(id);
    LuaStackOp<Object *>::push(L, obj);

    return 1;
}

static int luaGD_class_no_ctor(lua_State *L) {
    const char *class_name = lua_tostring(L, lua_upvalueindex(1));
    luaL_error(L, "class %s is not instantiable", class_name);
}

#define LUAGD_CLASS_METAMETHOD                                              \
    int class_idx = lua_tointeger(L, lua_upvalueindex(1));                  \
    Vector<ApiClass> *classes = luaGD_lightudataup<Vector<ApiClass>>(L, 2); \
                                                                            \
    ApiClass *current_class = &classes->ptrw()[class_idx];

#define INHERIT_OR_BREAK                                             \
    if (current_class->parent_idx >= 0)                              \
        current_class = &classes->ptrw()[current_class->parent_idx]; \
    else                                                             \
        break;

static LuauScriptInstance *get_script_instance(lua_State *L) {
    Object *self = LuaStackOp<Object *>::get(L, 1);
    Ref<LuauScript> script = self->get_script();

    if (script.is_valid() && script->_instance_has(self))
        return script->instance_get(self);

    return nullptr;
}

static void handle_object_returned(Object *obj) {
    // if Godot returns a RefCounted from a method, it is always in the form of a Ref.
    // as such, the RefCounted we receive will be initialized at a refcount of 1
    // and is considered "initialized" (first Ref already made).
    // we need to decrement the refcount by 1 after pushing to Luau to avoid leak.
    RefCounted *rc = Object::cast_to<RefCounted>(obj);

    if (rc != nullptr)
        rc->unreference();
}

static int call_class_method(lua_State *L, const ApiClass &g_class, ApiClassMethod &method) {
    ThreadPermissions permissions;
    if (method.permissions != -1)
        permissions = (ThreadPermissions)method.permissions;
    else
        permissions = g_class.default_permissions;

    luaGD_checkpermissions(L, method.debug_name, permissions);

    Vector<Variant> varargs;
    Vector<LuauVariant> args;
    Vector<const void *> pargs;

    get_arguments<ApiClassMethod, ApiClassArgument>(L, varargs, args, pargs, method);

    GDExtensionObjectPtr self = nullptr;

    if (!method.is_static) {
        LuauVariant self_var;
        self_var.lua_check(L, 1, GDEXTENSION_VARIANT_TYPE_OBJECT, g_class.name);

        self = *self_var.get_object();
    }

    GDExtensionMethodBindPtr method_bind = method.try_get_method_bind();

    if (method.is_vararg) {
        Variant ret;
        GDExtensionCallError error;
        internal::gde_interface->object_method_bind_call(method_bind, self, pargs.ptr(), pargs.size(), &ret, &error);

        if (method.return_type.type != -1) {
            LuaStackOp<Variant>::push(L, ret);

            if (ret.get_type() == Variant::OBJECT)
                handle_object_returned(ret.operator Object *());

            return 1;
        }

        return 0;
    } else {
        LuauVariant ret;
        void *ret_ptr = nullptr;

        if (method.return_type.type != -1) {
            ret.initialize((GDExtensionVariantType)method.return_type.type);
            ret_ptr = ret.get_opaque_pointer();
        }

        internal::gde_interface->object_method_bind_ptrcall(method_bind, self, pargs.ptr(), ret_ptr);

        if (ret.get_type() != -1) {
            ret.lua_push(L);

            // handle ref returned from Godot
            if (ret.get_type() == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                Object *obj = ObjectDB::get_instance(internal::gde_interface->object_get_instance_id(*ret.get_object()));
                handle_object_returned(obj);
            }

            return 1;
        }

        return 0;
    }
}

static int luaGD_class_method(lua_State *L) {
    const ApiClass *g_class = luaGD_lightudataup<ApiClass>(L, 1);
    ApiClassMethod *method = luaGD_lightudataup<ApiClassMethod>(L, 2);

    return call_class_method(L, *g_class, *method);
}

static void push_class_method(lua_State *L, const ApiClass &g_class, ApiClassMethod &method) {
    lua_pushlightuserdata(L, (void *)&g_class);
    lua_pushlightuserdata(L, &method);
    lua_pushcclosure(L, luaGD_class_method, method.debug_name, 2);
}

static int luaGD_class_namecall(lua_State *L) {
    LUAGD_CLASS_METAMETHOD

    if (const char *name = lua_namecallatom(L, nullptr)) {
        if (LuauScriptInstance *inst = get_script_instance(L)) {
            if (inst->has_method(name)) {
                int arg_count = lua_gettop(L) - 1;

                Vector<Variant> args;
                args.resize(arg_count);

                Vector<const Variant *> pargs;
                pargs.resize(arg_count);

                for (int i = 2; i <= lua_gettop(L); i++) {
                    args.set(i - 2, LuaStackOp<Variant>::get(L, i));
                    pargs.set(i - 2, &args[i - 2]);
                }

                Variant ret;
                GDExtensionCallError err;
                inst->call(name, pargs.ptr(), args.size(), &ret, &err);

                switch (err.error) {
                    case GDEXTENSION_CALL_OK:
                        LuaStackOp<Variant>::push(L, ret);
                        return 1;

                    case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT:
                        luaL_error(L, "invalid argument #%d to '%s' (%s expected, got %s)",
                                err.argument + 1,
                                name,
                                Variant::get_type_name((Variant::Type)err.expected).utf8().get_data(),
                                Variant::get_type_name(args[err.argument].get_type()).utf8().get_data());

                    case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
                        luaL_error(L, "missing argument #%d to '%s' (expected at least %d)",
                                args.size() + 2,
                                name,
                                err.argument);

                    case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
                        luaL_error(L, "too many arguments to '%s' (expected at most %d)",
                                name,
                                err.argument);

                    default:
                        luaL_error(L, "unknown error occurred when calling '%s'", name);
                }
            } else {
                int nargs = lua_gettop(L);

                LuaStackOp<String>::push(L, name);
                bool is_valid = inst->table_get(L);

                if (is_valid) {
                    if (lua_isfunction(L, -1)) {
                        lua_insert(L, -nargs - 1);
                        lua_call(L, nargs, LUA_MULTRET);

                        return lua_gettop(L);
                    } else {
                        lua_pop(L, 1);
                    }
                }
            }
        }

        while (true) {
            if (current_class->methods.has(name))
                return call_class_method(L, *current_class, current_class->methods[name]);

            INHERIT_OR_BREAK
        }

        luaL_error(L, "%s is not a valid method of %s", name, current_class->name.utf8().get_data());
    }

    luaL_error(L, "no namecallatom");
}

static int call_property_setget(lua_State *L, const ApiClass &g_class, const ApiClassProperty &property, ApiClassMethod &method) {
    if (property.index != -1) {
        lua_pushinteger(L, property.index);
        lua_insert(L, 2);
    }

    return call_class_method(L, g_class, method);
}

static int luaGD_class_index(lua_State *L) {
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    bool attempt_table_get = false;
    LuauScriptInstance *inst = get_script_instance(L);

    if (inst != nullptr) {
        if (const GDClassProperty *prop = inst->get_property(key)) {
            if (prop->setter != StringName() && prop->getter == StringName())
                luaL_error(L, "property '%s' is write-only", key);

            Variant ret;
            LuauScriptInstance::PropertySetGetError err;
            bool is_valid = inst->get(key, ret, &err);

            if (is_valid) {
                LuaStackOp<Variant>::push(L, ret);
                return 1;
            } else if (err == LuauScriptInstance::PROP_GET_FAILED)
                luaL_error(L, "failed to get property '%s'; see previous errors for more information", key);
            else
                luaL_error(L, "failed to get property '%s': unknown error", key); // due to the checks above, this should hopefully never happen
        } else {
            // object properties should take precedence over arbitrary values
            attempt_table_get = true;
        }
    }

    while (true) {
        if (current_class->properties.has(key)) {
            lua_remove(L, 2); // key

            const ApiClassProperty &prop = current_class->properties[key];

            if (prop.getter == "")
                luaL_error(L, "property '%s' is write-only", key);

            return call_property_setget(L, *current_class, prop, current_class->methods[prop.getter]);
        }

        INHERIT_OR_BREAK
    }

    if (attempt_table_get) {
        // the key is already on the top of the stack
        bool is_valid = inst->table_get(L);

        if (is_valid)
            return 1;
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->name.utf8().get_data());
}

static int luaGD_class_newindex(lua_State *L) {
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    bool attempt_table_set = false;
    LuauScriptInstance *inst = get_script_instance(L);

    if (inst != nullptr) {
        if (const GDClassProperty *prop = inst->get_property(key)) {
            if (prop->getter != StringName() && prop->setter == StringName())
                luaL_error(L, "property '%s' is read-only", key);

            LuauScriptInstance::PropertySetGetError err;
            Variant val = LuaStackOp<Variant>::get(L, 3);
            bool is_valid = inst->set(key, val, &err);

            if (is_valid)
                return 0;
            else if (err == LuauScriptInstance::PROP_WRONG_TYPE)
                luaGD_valueerror(L, key,
                        Variant::get_type_name(val.get_type()).utf8().get_data(),
                        Variant::get_type_name((Variant::Type)prop->property.type).utf8().get_data());
            else if (err == LuauScriptInstance::PROP_SET_FAILED)
                luaL_error(L, "failed to set property '%s'; see previous errors for more information", key);
            else
                luaL_error(L, "failed to set property '%s': unknown error", key); // should never happen
        } else {
            // object properties should take precedence over arbitrary values
            attempt_table_set = true;
        }
    }

    while (true) {
        if (current_class->properties.has(key)) {
            lua_remove(L, 2); // key

            const ApiClassProperty &prop = current_class->properties[key];

            if (prop.setter == "")
                luaL_error(L, "property '%s' is read-only", key);

            return call_property_setget(L, *current_class, prop, current_class->methods[prop.setter]);
        }

        INHERIT_OR_BREAK
    }

    if (attempt_table_set) {
        // key and value are already on the top of the stack
        bool is_valid = inst->table_set(L);
        if (is_valid)
            return 0;
    }

    luaL_error(L, "%s is not a valid member of %s", key, current_class->name.utf8().get_data());
}

static int luaGD_class_singleton_getter(lua_State *L) {
    ApiClass *g_class = luaGD_lightudataup<ApiClass>(L, 1);
    if (lua_gettop(L) > 0)
        luaL_error(L, "singleton getter takes no arguments");

    Object *singleton = g_class->try_get_singleton();
    if (singleton == nullptr)
        luaL_error(L, "could not get singleton '%s'", g_class->name.utf8().get_data());

    LuaStackOp<Object *>::push(L, singleton);
    return 1;
}

void luaGD_openclasses(lua_State *L) {
    LUAGD_LOAD_GUARD(L, "_gdClassesLoaded");

    ExtensionApi &extension_api = get_extension_api();

    ApiClass *classes = extension_api.classes.ptrw();

    for (int i = 0; i < extension_api.classes.size(); i++) {
        ApiClass &g_class = classes[i];

        luaGD_newlib(L, g_class.name.utf8().get_data(), g_class.metatable_name);

        // Enums
        for (const ApiEnum &class_enum : g_class.enums) {
            push_enum(L, class_enum);
            lua_setfield(L, -3, class_enum.name);
        }

        // Constants
        for (const ApiConstant &constant : g_class.constants) {
            lua_pushinteger(L, constant.value);
            lua_setfield(L, -3, constant.name);
        }

        // Constructor (global __call)
        LuaStackOp<String>::push(L, g_class.name);
        lua_pushcclosure(L,
                g_class.is_instantiable
                        ? luaGD_class_ctor
                        : luaGD_class_no_ctor,
                g_class.constructor_debug_name, 1);
        lua_setfield(L, -2, "__call");

        // Methods (__namecall)
        if (g_class.methods.size() > 0) {
            lua_pushinteger(L, i);
            lua_pushlightuserdata(L, &extension_api.classes);
            lua_pushcclosure(L, luaGD_class_namecall, g_class.namecall_debug_name, 2);
            lua_setfield(L, -4, "__namecall");
        }

        // All methods (global table)
        for (KeyValue<String, ApiClassMethod> &pair : g_class.methods) {
            push_class_method(L, g_class, pair.value);
            lua_setfield(L, -3, pair.value.name.utf8().get_data());
        }

        for (ApiClassMethod &static_method : g_class.static_methods) {
            push_class_method(L, g_class, static_method);
            lua_setfield(L, -3, static_method.name.utf8().get_data());
        }

        // TODO Signals ?

        // Properties (__newindex, __index)
        lua_pushinteger(L, i);
        lua_pushlightuserdata(L, &extension_api.classes);
        lua_pushcclosure(L, luaGD_class_newindex, g_class.newindex_debug_name, 2);
        lua_setfield(L, -4, "__newindex");

        lua_pushinteger(L, i);
        lua_pushlightuserdata(L, &extension_api.classes);
        lua_pushcclosure(L, luaGD_class_index, g_class.index_debug_name, 2);
        lua_setfield(L, -4, "__index");

        // Singleton
        lua_pushlightuserdata(L, &g_class);
        lua_pushcclosure(L, luaGD_class_singleton_getter, g_class.singleton_getter_debug_name, 1);
        lua_setfield(L, -3, "GetSingleton");

        luaGD_poplib(L, true);
    }
}

/////////////
// Globals //
/////////////

static int luaGD_utility_function(lua_State *L) {
    const ApiUtilityFunction *func = luaGD_lightudataup<ApiUtilityFunction>(L, 1);

    Vector<Variant> varargs;
    Vector<LuauVariant> args;
    Vector<const void *> pargs;

    int nargs = get_arguments<ApiUtilityFunction, ApiArgumentNoDefault>(L, varargs, args, pargs, *func);

    if (func->return_type == -1) {
        func->func(nullptr, pargs.ptr(), nargs);
        return 0;
    } else {
        LuauVariant ret;
        ret.initialize((GDExtensionVariantType)func->return_type);

        func->func(ret.get_opaque_pointer(), pargs.ptr(), nargs);

        ret.lua_push(L);
        return 1;
    }
}

void luaGD_openglobals(lua_State *L) {
    LUAGD_LOAD_GUARD(L, "_gdGlobalsLoaded")

    const ExtensionApi &api = get_extension_api();

    // Enum
    lua_createtable(L, 0, api.global_enums.size());

    for (const ApiEnum &global_enum : api.global_enums) {
        push_enum(L, global_enum);
        lua_setfield(L, -2, global_enum.name);
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Enum");

    // Constants
    // does this work? idk
    lua_createtable(L, 0, api.global_constants.size());

    for (const ApiConstant &global_constant : api.global_constants) {
        lua_pushinteger(L, global_constant.value);
        lua_setfield(L, -2, global_constant.name);
    }

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Constants");

    // Utility functions
    for (const ApiUtilityFunction &utility_function : api.utility_functions) {
        lua_pushlightuserdata(L, (void *)&utility_function);
        lua_pushcclosure(L, luaGD_utility_function, utility_function.debug_name, 1);
        lua_setglobal(L, utility_function.name.utf8().get_data());
    }
}

//////////////////////////
// Builtin/class common //
//////////////////////////

static int luaGD_variant_tostring(lua_State *L) {
    // Special case - freed objects
    if (LuaStackOp<Object *>::is(L, 1) && LuaStackOp<Object *>::get(L, 1) == nullptr)
        lua_pushstring(L, "<Freed Object>");
    else {
        Variant v = LuaStackOp<Variant>::check(L, 1);
        String str = v.stringify();
        lua_pushstring(L, str.utf8().get_data());
    }

    return 1;
}

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name) {
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L); // global table
    lua_createtable(L, 0, 3); // global metatable - assume 3 fields: __fortype, __call, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    // for typeof and type errors
    lua_pushstring(L, global_name);
    lua_setfield(L, -4, "__type");

    lua_pushcfunction(L, luaGD_variant_tostring, "Variant.__tostring");
    lua_setfield(L, -4, "__tostring");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}

void luaGD_poplib(lua_State *L, bool is_obj) {
    if (is_obj) {
        lua_pushboolean(L, true);
        lua_setfield(L, -4, "__isgdobj");
    }

    // global will be set readonly on sandbox
    lua_setreadonly(L, -3, true); // instance metatable
    lua_setreadonly(L, -1, true); // global metatable

    lua_pop(L, 3);
}
