#include "luagd_bindings.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <type_traits>

#include "extension_api.h"
#include "gd_luau.h"
#include "godot_cpp/classes/global_constants.hpp"
#include "luagd.h"
#include "luagd_bindings_stack.gen.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_utils.h"
#include "luagd_variant.h"
#include "luau_lib.h"
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

static int luaGD_global_index(lua_State *L) {
    const char *name = lua_tostring(L, lua_upvalueindex(1));

    // Copy key to front of stack
    lua_pushvalue(L, 2);
    lua_insert(L, 1);

    lua_rawget(L, 2);

    if (lua_isnil(L, -1))
        luaGD_indexerror(L, luaL_checkstring(L, 1), name);

    lua_pop(L, 1); // key

    return 1;
}

static void push_enum(lua_State *L, const ApiEnum &p_enum) { // notation cause reserved keyword
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

// Getters for argument types
template <typename T>
_FORCE_INLINE_ static GDExtensionVariantType get_arg_type(const T &arg) { return arg.type; }

template <>
_FORCE_INLINE_ GDExtensionVariantType get_arg_type<ApiClassArgument>(const ApiClassArgument &arg) { return (GDExtensionVariantType)arg.type.type; }

template <typename T>
_FORCE_INLINE_ static String get_arg_type_name(const T &arg) { return String(); }

template <>
_FORCE_INLINE_ String get_arg_type_name<ApiClassArgument>(const ApiClassArgument &arg) { return arg.type.type_name; }

template <>
_FORCE_INLINE_ String get_arg_type_name<GDProperty>(const GDProperty &arg) { return arg.class_name; }

// Getters for method types
template <typename T>
_FORCE_INLINE_ static bool is_method_static(const T &method) { return method.is_static; }

template <>
_FORCE_INLINE_ bool is_method_static<ApiUtilityFunction>(const ApiUtilityFunction &) { return true; }

template <>
_FORCE_INLINE_ bool is_method_static<GDMethod>(const GDMethod &) { return false; }

template <typename T>
_FORCE_INLINE_ static bool is_method_vararg(const T &method) { return method.is_vararg; }

template <>
_FORCE_INLINE_ bool is_method_vararg<GDMethod>(const GDMethod &method) { return true; }

// From stack
template <typename T>
_FORCE_INLINE_ static void get_argument(lua_State *L, int idx, const T &arg, LuauVariant &out) {
    out.lua_check(L, idx, arg.type);
}

template <>
_FORCE_INLINE_ void get_argument<ApiClassArgument>(lua_State *L, int idx, const ApiClassArgument &arg, LuauVariant &out) {
    const ApiClassType &type = arg.type;
    out.lua_check(L, idx, (GDExtensionVariantType)type.type, type.type_name);
}

// Defaults
template <typename T>
struct has_default_value_trait : std::true_type {};

template <>
struct has_default_value_trait<ApiArgumentNoDefault> : std::false_type {};

template <typename TMethod, typename TArg>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int arg_offset, int nargs, Vector<const void *> *pargs, const TMethod &method, std::true_type const &) {
    for (int i = nargs; i < method.arguments.size(); i++) {
        const TArg &arg = method.arguments[i];

        if (arg.has_default_value) {
            // Special case: null Object default value
            if (get_arg_type(arg) == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                if (*arg.default_value.get_object() != nullptr) {
                    // Should never happen
                    ERR_PRINT("could not set non-null object argument default value");
                }

                pargs->set(i, nullptr);
            } else {
                pargs->set(i, arg.default_value.get_opaque_pointer());
            }
        } else {
            LuauVariant dummy;
            get_argument(L, i + 1 + arg_offset, arg, dummy);
        }
    }
}

template <>
_FORCE_INLINE_ void get_default_args<GDMethod, GDProperty>(
        lua_State *L, int arg_offset, int nargs, Vector<const void *> *pargs, const GDMethod &method, std::true_type const &) {
    int args_allowed = method.arguments.size();
    int args_default = method.default_arguments.size();
    int args_required = args_allowed - args_default;

    for (int i = nargs; i < args_allowed; i++) {
        const GDProperty &arg = method.arguments[i];

        if (i >= args_required) {
            pargs->set(i, &method.default_arguments[i - args_required]);
        } else {
            LuauVariant dummy;
            get_argument(L, i + 1 + arg_offset, arg, dummy);
        }
    }
}

template <typename TMethod, typename>
_FORCE_INLINE_ static void get_default_args(lua_State *L, int arg_offset, int nargs, Vector<const void *> *pargs, const TMethod &method, std::false_type const &) {
    LuauVariant dummy;
    get_argument(L, nargs + 1 + arg_offset, method.arguments[nargs], dummy);
}

// this is magic
template <typename T, typename TArg>
static int get_arguments(lua_State *L,
        const char *method_name,
        Vector<Variant> *varargs,
        Vector<LuauVariant> *args,
        Vector<const void *> *pargs,
        const T &method) {
    // arg 1 is self for instance methods
    int arg_offset = is_method_static(method) ? 0 : 1;
    int nargs = lua_gettop(L) - arg_offset;

    if (method.arguments.size() > nargs)
        pargs->resize(method.arguments.size());
    else
        pargs->resize(nargs);

    if (is_method_vararg(method)) {
        varargs->resize(nargs);

        LuauVariant arg;

        for (int i = 0; i < nargs; i++) {
            if (i < method.arguments.size()) {
                get_argument(L, i + 1 + arg_offset, method.arguments[i], arg);
                varargs->set(i, arg.to_variant());
            } else {
                varargs->set(i, LuaStackOp<Variant>::get(L, i + 1 + arg_offset));
            }

            pargs->set(i, &varargs->operator[](i));
        }
    } else {
        args->resize(nargs);

        if (nargs > method.arguments.size())
            luaL_error(L, "too many arguments to '%s' (expected at most %d)", method_name, method.arguments.size());

        LuauVariant *args_ptr = args->ptrw();

        for (int i = 0; i < nargs; i++) {
            get_argument(L, i + 1 + arg_offset, method.arguments[i], args->ptrw()[i]);
            pargs->set(i, args_ptr[i].get_opaque_pointer_arg());
        }
    }

    if (nargs < method.arguments.size())
        get_default_args<T, TArg>(L, arg_offset, nargs, pargs, method, has_default_value_trait<TArg>());

    return nargs;
}

//////////////////////////
// Builtin/class common //
//////////////////////////

#define VARIANT_TOSTRING_DEBUG_NAME "Variant.__tostring"

static int luaGD_variant_tostring(lua_State *L) {
    // Special case - freed objects
    if (LuaStackOp<Object *>::is(L, 1) && LuaStackOp<Object *>::get(L, 1) == nullptr) {
        lua_pushstring(L, "<Freed Object>");
    } else {
        Variant v = LuaStackOp<Variant>::check(L, 1);
        String str = v.stringify();
        lua_pushstring(L, str.utf8().get_data());
    }

    return 1;
}

void luaGD_newlib(lua_State *L, const char *global_name, const char *mt_name) {
    luaL_newmetatable(L, mt_name); // instance metatable
    lua_newtable(L); // global table
    lua_createtable(L, 0, 2); // global metatable - assume 2 fields: __fortype, __index

    lua_pushstring(L, mt_name);
    lua_setfield(L, -2, "__fortype");

    // global index
    lua_pushstring(L, global_name);
    lua_pushcclosure(L, luaGD_global_index, "_G.__index", 1); // TODO: name?
    lua_setfield(L, -2, "__index");

    // for typeof and type errors
    lua_pushstring(L, global_name);
    lua_setfield(L, -4, "__type");

    lua_pushcfunction(L, luaGD_variant_tostring, VARIANT_TOSTRING_DEBUG_NAME);
    lua_setfield(L, -4, "__tostring");

    // set global table's metatable
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -3);

    lua_pushvalue(L, -2);
    lua_setglobal(L, global_name);
}

void luaGD_poplib(lua_State *L, bool is_obj, int class_idx = -1) {
    if (is_obj) {
        lua_pushinteger(L, class_idx);
        lua_setfield(L, -4, "__gdclass");
    }

    // global will be set readonly on sandbox
    lua_setreadonly(L, -3, true); // instance metatable
    lua_setreadonly(L, -1, true); // global metatable

    lua_pop(L, 3);
}

static LuauScriptInstance *get_script_instance(Object *self) {
    if (self == nullptr)
        return nullptr;

    Ref<LuauScript> script = self->get_script();

    if (script.is_valid() && script->_instance_has(self))
        return script->instance_get(self);

    return nullptr;
}

static ThreadPermissions get_method_permissions(const ApiClass &g_class, const ApiClassMethod &method) {
    return method.permissions != PERMISSION_INHERIT ? method.permissions : g_class.default_permissions;
}

//////////////
// Builtins //
//////////////

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

    // Lua is 1 indexed :))))
    if (idx > array->size()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, idx);
    LuaStackOp<TElem>::push(L, array->operator[](idx - 1));
    return 2;
}

static int luaGD_array_iter(lua_State *L) {
    lua_pushvalue(L, lua_upvalueindex(1)); // next
    lua_pushvalue(L, 1); // array
    lua_pushinteger(L, 0); // initial index

    return 3;
}

struct ArrayTypeInfo {
    const char *len_debug_name;
    lua_CFunction len;

    const char *iter_next_debug_name;
    lua_CFunction iter_next;
};

#define ARRAY_INFO(type, elem_type)         \
    static const ArrayTypeInfo type##_info{ \
        BUILTIN_MT_NAME(type) ".__len",     \
        luaGD_array_len<type>,              \
        BUILTIN_MT_NAME(type) ".next",      \
        luaGD_array_next<type, elem_type>   \
    };                                      \
                                            \
    return &type##_info;

// ! sync with any new arrays
static const ArrayTypeInfo *get_array_type_info(GDExtensionVariantType type) {
    switch (type) {
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

        default:
            return nullptr;
    }
}

static int luaGD_builtin_ctor(lua_State *L) {
    const ApiBuiltinClass *builtin_class = luaGD_lightudataup<ApiBuiltinClass>(L, 1);
    const char *error_string = lua_tostring(L, lua_upvalueindex(2));

    int nargs = lua_gettop(L);

    Vector<LuauVariant> args;
    args.resize(nargs);

    Vector<const void *> pargs;
    pargs.resize(nargs);

    for (const ApiVariantConstructor &ctor : builtin_class->constructors) {
        if (nargs != ctor.arguments.size())
            continue;

        bool valid = true;

        LuauVariant *args_ptr = args.ptrw();

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
            pargs.set(i, args_ptr[i].get_opaque_pointer_arg());
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
    const Vector<ApiClass> *classes = luaGD_lightudataup<Vector<ApiClass>>(L, 1);

    int class_idx = -1;

    {
        // Get class index.
        if (lua_getmetatable(L, 1)) {
            lua_getfield(L, -1, "__gdclass");

            if (lua_isnumber(L, -1)) {
                class_idx = lua_tointeger(L, -1);
            }

            lua_pop(L, 2); // value, metatable
        }

        if (class_idx == -1)
            luaL_typeerrorL(L, 1, "Object");
    }

    Object *obj = LuaStackOp<Object *>::get(L, 1);
    const char *method = luaL_checkstring(L, 2);

    // Check if instance has method.
    if (LuauScriptInstance *inst = get_script_instance(obj)) {
        if (inst->has_method(method)) {
            LuaStackOp<Callable>::push(L, Callable(obj, method));
            return 1;
        }
    }

    // Check if class or its parents have method.
    while (class_idx != -1) {
        const ApiClass *g_class = &classes->get(class_idx);

        HashMap<String, ApiClassMethod>::ConstIterator E = g_class->methods.find(method);
        if (E) {
            luaGD_checkpermissions(L, E->value.debug_name, get_method_permissions(*g_class, E->value));

            LuaStackOp<Callable>::push(L, Callable(obj, E->value.gd_name));
            return 1;
        }

        class_idx = g_class->parent_idx;
    }

    luaGD_nomethoderror(L, method, "this object");
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
        luaL_error(L, "type '%s' is read-only", builtin_class->name);
    else
        luaGD_indexerror(L, key.operator String().utf8().get_data(), builtin_class->name);
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

    luaGD_indexerror(L, key.operator String().utf8().get_data(), builtin_class->name);
}

static int call_builtin_method(lua_State *L, const ApiBuiltinClass &builtin_class, const ApiVariantMethod &method) {
    Vector<Variant> varargs;
    Vector<LuauVariant> args;
    Vector<const void *> pargs;

    get_arguments<ApiVariantMethod, ApiArgument>(L, method.name, &varargs, &args, &pargs, method);

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

        luaGD_nomethoderror(L, name, builtin_class->name);
    }

    luaGD_nonamecallatomerror(L);
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

static void luaGD_builtin_unbound(lua_State *L, const char *type_name, const char *metatable_name) {
    luaL_newmetatable(L, metatable_name);

    lua_pushstring(L, type_name);
    lua_setfield(L, -2, "__type");

    lua_pushcfunction(L, luaGD_variant_tostring, VARIANT_TOSTRING_DEBUG_NAME);
    lua_setfield(L, -2, "__tostring");

    lua_setreadonly(L, -1, true);
    lua_pop(L, 1); // metatable
}

void luaGD_openbuiltins(lua_State *L) {
    LUAGD_LOAD_GUARD(L, "_gdBuiltinsLoaded");

    const ExtensionApi &extension_api = get_extension_api();

    for (const ApiBuiltinClass &builtin_class : extension_api.builtin_classes) {
        luaGD_newlib(L, builtin_class.name, builtin_class.metatable_name);

        // Enums
        for (const ApiEnum &class_enum : builtin_class.enums) {
            push_enum(L, class_enum);
            lua_setfield(L, -3, class_enum.name);
        }

        // Constants
        for (const ApiVariantConstant &constant : builtin_class.constants) {
            LuaStackOp<Variant>::push(L, constant.value);
            lua_setfield(L, -3, constant.name);
        }

        // Constructors (global .new)
        if (builtin_class.type == GDEXTENSION_VARIANT_TYPE_CALLABLE) { // Special case for Callable security
            lua_pushlightuserdata(L, (void *)&extension_api.classes);
            lua_pushcclosure(L, luaGD_callable_ctor, "Callable.new", 1);
            lua_setfield(L, -3, "new");
        } else {
            lua_pushlightuserdata(L, (void *)&builtin_class);
            lua_pushstring(L, builtin_class.constructor_error_string);
            lua_pushcclosure(L, luaGD_builtin_ctor, builtin_class.constructor_debug_name, 2);
            lua_setfield(L, -3, "new");
        }

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
            lua_setfield(L, -3, pair.value.name);
        }

        for (const ApiVariantMethod &static_method : builtin_class.static_methods) {
            push_builtin_method(L, builtin_class, static_method);
            lua_setfield(L, -3, static_method.name);
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

                default:
                    ERR_FAIL_MSG("variant operator not handled");
            }

            lua_setfield(L, -4, op_mt_name);
        }

        // Array type handling
        if (const ArrayTypeInfo *arr_type_info = get_array_type_info(builtin_class.type)) {
            // __len
            lua_pushcfunction(L, arr_type_info->len, arr_type_info->len_debug_name);
            lua_setfield(L, -4, "__len");

            // __iter
            lua_pushcfunction(L, arr_type_info->iter_next, arr_type_info->iter_next_debug_name);
            lua_pushcclosure(L, luaGD_array_iter, BUILTIN_MT_PREFIX "Array.__iter", 1);
            lua_setfield(L, -4, "__iter");
        }

        luaGD_poplib(L, false);
    }

    // Special cases
    luaGD_builtin_unbound(L, "StringName", BUILTIN_MT_NAME(StringName));
    luaGD_builtin_unbound(L, "NodePath", BUILTIN_MT_NAME(NodePath));
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

#define LUAGD_CLASS_METAMETHOD                                              \
    int class_idx = lua_tointeger(L, lua_upvalueindex(1));                  \
    Vector<ApiClass> *classes = luaGD_lightudataup<Vector<ApiClass>>(L, 2); \
                                                                            \
    ApiClass *current_class = &classes->ptrw()[class_idx];                  \
                                                                            \
    Object *self = LuaStackOp<Object *>::check(L, 1);                       \
    if (self == nullptr)                                                    \
        luaL_error(L, "Object is null or freed");

#define INHERIT_OR_BREAK                                             \
    if (current_class->parent_idx >= 0)                              \
        current_class = &classes->ptrw()[current_class->parent_idx]; \
    else                                                             \
        break;

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
    luaGD_checkpermissions(L, method.debug_name, get_method_permissions(g_class, method));

    Vector<Variant> varargs;
    Vector<LuauVariant> args;
    Vector<const void *> pargs;

    get_arguments<ApiClassMethod, ApiClassArgument>(L, method.name, &varargs, &args, &pargs, method);

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
            if (ret.get_type() == GDEXTENSION_VARIANT_TYPE_OBJECT && *ret.get_object() != nullptr) {
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
        if (strcmp(name, "Free") == 0) {
            if (self->is_class("RefCounted"))
                luaL_error(L, "cannot free a RefCounted object");

            // Zero out the object to prevent segfaults
            *LuaStackOp<Object *>::get_ptr(L, 1) = 0;

            memdelete(self);
            return 0;
        }

        if (LuauScriptInstance *inst = get_script_instance(self)) {
            GDThreadData *udata = luaGD_getthreaddata(L);

            if (inst->get_vm_type() == udata->vm_type) {
                // Attempt to call from definition table.
                int nargs = lua_gettop(L);
                int nargs_to_send = nargs;

                // Type checking.
                if (const GDMethod *method = inst->get_method(name)) {
                    LuauVariant dummy;

                    int args_allowed = method->arguments.size();
                    int args_default = method->default_arguments.size();
                    int args_required = args_allowed - args_default;

                    // Sizes and start indices listed like this because it's confusing.
                    int first_arg_idx = 2;
                    int last_required_arg_idx = args_required + 1;

                    for (int i = first_arg_idx; i <= last_required_arg_idx; i++) {
                        const GDProperty &arg = method->arguments[i - first_arg_idx];
                        dummy.lua_check(L, i, arg.type, arg.class_name);
                    }

                    int first_default_idx = last_required_arg_idx + 1;
                    int last_arg_idx = args_allowed + 1;

                    for (int i = first_default_idx; i <= last_arg_idx; i++) {
                        const GDProperty &arg = method->arguments[i - first_arg_idx];

                        if (i > nargs)
                            LuaStackOp<Variant>::push(L, method->default_arguments[i - first_default_idx]);
                        else
                            dummy.lua_check(L, i, arg.type, arg.class_name);
                    }
                }

                LuauScript *s = inst->get_script().ptr();

                while (s != nullptr) {
                    lua_pushstring(L, name);
                    s->def_table_get(udata->vm_type, L);

                    if (lua_isfunction(L, -1)) {
                        lua_insert(L, 1);

                        // Must re-get nargs in case default arguments were added.
                        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);

                        return lua_gettop(L);
                    }

                    lua_pop(L, 1); // value

                    s = s->get_base().ptr();
                }
            }

            if (const GDMethod *method = inst->get_method(name)) {
                // Attempt to use instance `call` (cross-vm method call).
                Vector<Variant> varargs;
                Vector<const void *> pargs;

                get_arguments<GDMethod, GDProperty>(L, name, &varargs, nullptr, &pargs, *method);

                Variant ret;
                GDExtensionCallError err;
                inst->call(name, reinterpret_cast<const Variant *const *>(pargs.ptr()), pargs.size(), &ret, &err);

                // Error should have been sent out when getting arguments, so ignore it.

                LuaStackOp<Variant>::push(L, ret);
                return 1;
            }

            {
                // Attempt to call from instance table.
                int nargs = lua_gettop(L);

                lua_pushstring(L, name);
                bool is_valid = inst->table_get(L);

                if (is_valid) {
                    if (lua_isfunction(L, -1)) {
                        lua_insert(L, 1);
                        lua_call(L, nargs, LUA_MULTRET);

                        return lua_gettop(L);
                    }

                    lua_pop(L, 1); // value
                }
            }
        }

        while (true) {
            if (current_class->methods.has(name))
                return call_class_method(L, *current_class, current_class->methods[name]);

            INHERIT_OR_BREAK
        }

        luaGD_nomethoderror(L, name, current_class->name);
    }

    luaGD_nonamecallatomerror(L);
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
    LuauScriptInstance *inst = get_script_instance(self);

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
        } else if (const GDMethod *signal = inst->get_signal(key)) {
            LuaStackOp<Signal>::push(L, Signal(self, key));
            return 1;
        } else if (const Variant *constant = inst->get_constant(key)) {
            LuaStackOp<Variant>::push(L, *constant);
            return 1;
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

        HashMap<String, ApiClassSignal>::ConstIterator E = current_class->signals.find(key);

        if (E) {
            LuaStackOp<Signal>::push(L, Signal(self, E->value.gd_name));
            return 1;
        }

        INHERIT_OR_BREAK
    }

    if (attempt_table_get) {
        // the key is already on the top of the stack
        bool is_valid = inst->table_get(L);

        if (is_valid)
            return 1;
    }

    luaGD_indexerror(L, key, current_class->name);
}

#define SIGNAL_ASSIGN_ERROR luaL_error(L, "cannot assign to signal '%s'", key)

static int luaGD_class_newindex(lua_State *L) {
    LUAGD_CLASS_METAMETHOD

    const char *key = luaL_checkstring(L, 2);

    bool attempt_table_set = false;
    LuauScriptInstance *inst = get_script_instance(self);

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
        } else if (inst->get_signal(key) != nullptr) {
            SIGNAL_ASSIGN_ERROR;
        } else if (inst->get_constant(key) != nullptr) {
            luaL_error(L, "cannot assign to constant '%s'", key);
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

        if (current_class->signals.has(key))
            SIGNAL_ASSIGN_ERROR;

        INHERIT_OR_BREAK
    }

    if (attempt_table_set) {
        // key and value are already on the top of the stack
        bool is_valid = inst->table_set(L);
        if (is_valid)
            return 0;
    }

    luaGD_indexerror(L, key, current_class->name);
}

static int luaGD_class_singleton_getter(lua_State *L) {
    ApiClass *g_class = luaGD_lightudataup<ApiClass>(L, 1);
    if (lua_gettop(L) > 0)
        luaL_error(L, "singleton getter takes no arguments");

    Object *singleton = g_class->try_get_singleton();
    if (singleton == nullptr)
        luaL_error(L, "could not get singleton '%s'", g_class->name);

    LuaStackOp<Object *>::push(L, singleton);
    return 1;
}

void luaGD_openclasses(lua_State *L) {
    LUAGD_LOAD_GUARD(L, "_gdClassesLoaded");

    ExtensionApi &extension_api = get_extension_api();

    ApiClass *classes = extension_api.classes.ptrw();

    for (int i = 0; i < extension_api.classes.size(); i++) {
        ApiClass &g_class = classes[i];

        luaGD_newlib(L, g_class.name, g_class.metatable_name);

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

        // Constructor (global .new)
        if (g_class.is_instantiable) {
            lua_pushstring(L, g_class.name);
            lua_pushcclosure(L, luaGD_class_ctor, g_class.constructor_debug_name, 1);
            lua_setfield(L, -3, "new");
        }

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
            lua_setfield(L, -3, pair.value.name);
        }

        for (ApiClassMethod &static_method : g_class.static_methods) {
            push_class_method(L, g_class, static_method);
            lua_setfield(L, -3, static_method.name);
        }

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

        luaGD_poplib(L, true, i);
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

    int nargs = get_arguments<ApiUtilityFunction, ApiArgumentNoDefault>(L, func->name, &varargs, &args, &pargs, *func);

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
        lua_pushlightuserdata(L, (void *)&utility_function);
        lua_pushcclosure(L, luaGD_utility_function, utility_function.debug_name, 1);
        lua_setglobal(L, utility_function.name);
    }
}
