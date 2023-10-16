#include "luagd_bindings.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <type_traits>

#include "error_strings.h"
#include "extension_api.h"
#include "luagd_bindings_stack.gen.h"
#include "luagd_lib.h"
#include "luagd_permissions.h"
#include "luagd_stack.h"
#include "luagd_variant.h"
#include "luau_lib.h"
#include "luau_script.h"
#include "services/pck_scanner.h"
#include "utils.h"
#include "wrapped_no_binding.h"

/////////////
// Generic //
/////////////

static int luaGD_global_index(lua_State *L) {
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    luaGD_indexerror(L, luaL_checkstring(L, 2), name);

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

#if TOOLS_ENABLED
#define SET_CALL_STACK(L) LuauLanguage::get_singleton()->set_call_stack(L)
#define CLEAR_CALL_STACK LuauLanguage::get_singleton()->clear_call_stack()
#else
#define SET_CALL_STACK(L)
#define CLEAR_CALL_STACK
#endif

/* GETTING ARGUMENTS */

// Getters for argument types
template <typename T>
_FORCE_INLINE_ static GDExtensionVariantType get_arg_type(const T &p_arg) { return p_arg.type; }

template <>
_FORCE_INLINE_ GDExtensionVariantType get_arg_type<ApiClassArgument>(const ApiClassArgument &p_arg) { return (GDExtensionVariantType)p_arg.type.type; }

template <typename T>
_FORCE_INLINE_ static String get_arg_type_name(const T &) { return String(); }

template <>
_FORCE_INLINE_ String get_arg_type_name<ApiClassArgument>(const ApiClassArgument &p_arg) { return p_arg.type.type_name; }

template <>
_FORCE_INLINE_ String get_arg_type_name<GDProperty>(const GDProperty &p_arg) { return p_arg.class_name; }

// Getters for method types
template <typename T>
_FORCE_INLINE_ static bool is_method_static(const T &p_method) { return p_method.is_static; }

template <>
_FORCE_INLINE_ bool is_method_static<ApiUtilityFunction>(const ApiUtilityFunction &) { return true; }

template <>
_FORCE_INLINE_ bool is_method_static<GDMethod>(const GDMethod &) { return false; }

template <typename T>
_FORCE_INLINE_ static bool is_method_vararg(const T &p_method) { return p_method.is_vararg; }

template <>
_FORCE_INLINE_ bool is_method_vararg<GDMethod>(const GDMethod &) { return true; }

// From stack
template <typename T>
_FORCE_INLINE_ static void get_argument(lua_State *L, int p_idx, const T &p_arg, LuauVariant &r_out) {
    r_out.lua_check(L, p_idx, p_arg.type);
}

template <>
_FORCE_INLINE_ void get_argument<ApiClassArgument>(lua_State *L, int idx, const ApiClassArgument &p_arg, LuauVariant &r_out) {
    const ApiClassType &type = p_arg.type;
    r_out.lua_check(L, idx, (GDExtensionVariantType)type.type, type.type_name);
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
            if (get_arg_type(arg) == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                if (*arg.default_value.template get_ptr<GDExtensionObjectPtr>()) {
                    // Should never happen
                    ERR_PRINT(NON_NULL_OBJ_DEFAULT_ARG_ERR);
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
template <typename T, typename TArg>
static int get_arguments(lua_State *L,
        const char *p_method_name,
        LocalVector<Variant> *r_varargs,
        LocalVector<LuauVariant> *r_args,
        LocalVector<const void *> *r_pargs,
        const T &p_method) {
    // arg 1 is self for instance methods
    int arg_offset = is_method_static(p_method) ? 0 : 1;
    int nargs = lua_gettop(L) - arg_offset;

    if (p_method.arguments.size() > nargs)
        r_pargs->resize(p_method.arguments.size());
    else
        r_pargs->resize(nargs);

    if (is_method_vararg(p_method)) {
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
            luaGD_toomanyargserror(L, p_method_name, p_method.arguments.size());

        for (int i = 0; i < nargs; i++) {
            get_argument(L, i + 1 + arg_offset, p_method.arguments[i], r_args->ptr()[i]);
            r_pargs->ptr()[i] = r_args->ptr()[i].get_opaque_pointer();
        }
    }

    if (nargs < p_method.arguments.size())
        get_default_args<T, TArg>(L, arg_offset, nargs, r_pargs, p_method, has_default_value_trait<TArg>());

    return nargs;
}

//////////////////////////
// Builtin/class common //
//////////////////////////

#define VARIANT_TOSTRING_DEBUG_NAME "Variant.__tostring"

static int luaGD_variant_tostring(lua_State *L) {
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

static void luaGD_initmetatable(lua_State *L, int p_idx, GDExtensionVariantType p_variant_type, const char *p_global_name) {
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

static void luaGD_initglobaltable(lua_State *L, int p_idx, const char *p_global_name) {
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

static ThreadPermissions get_method_permissions(const ApiClass &p_class, const ApiClassMethod &p_method) {
    return p_method.permissions != PERMISSION_INHERIT ? p_method.permissions : p_class.default_permissions;
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

    luaL_error(L, DICT_ITER_PREV_KEY_MISSING_ERR);
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

                // lua is 1 indexed :))))
                builtin_class->indexed_setter(self.get_opaque_pointer(), key.operator int64_t() - 1, val.get_opaque_pointer());
                return 0;
            }

            if (builtin_class->keyed_setter) {
                // Keyed
                // if the key or val is ever assumed to not be Variant, this will segfault. nice.
                Variant val = LuaStackOp<Variant>::check(L, 3);
                builtin_class->keyed_setter(self.get_opaque_pointer(), &key, &val);
                return 0;
            }

            luaL_error(L, NO_INDEXED_KEYED_SETTER_ERR, builtin_class->name);
        }

        if (strcmp(name, "Get") == 0) {
            LuauVariant self;
            self.lua_check(L, 1, builtin_class->type);

            Variant key = LuaStackOp<Variant>::check(L, 2);

            if (builtin_class->indexed_getter && key.get_type() == Variant::INT) {
                // Indexed
                LuauVariant ret;
                ret.initialize(builtin_class->indexing_return_type);

                // lua is 1 indexed :))))
                builtin_class->indexed_getter(self.get_opaque_pointer(), key.operator int64_t() - 1, ret.get_opaque_pointer());

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
                    luaGD_keyindexerror(L, builtin_class->name, key.stringify().utf8().get_data());
                }
            }

            luaL_error(L, NO_INDEXED_KEYED_SETTER_ERR, builtin_class->name);
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

    // TODO: say something better here
    luaL_error(L, NO_OP_MATCHED_ERR);
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

                    default:
                        ERR_FAIL_MSG(OP_NOT_HANDLED_ERR);
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

        // All methods (global table)
        for (const KeyValue<String, ApiVariantMethod> &pair : builtin_class.methods) {
            push_builtin_method(L, builtin_class, pair.value);
            lua_setfield(L, -2, pair.value.name);
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

/////////////
// Classes //
/////////////

static int luaGD_class_ctor(lua_State *L) {
    StringName class_name = lua_tostring(L, lua_upvalueindex(1));

    GDExtensionObjectPtr obj = internal::gdextension_interface_classdb_construct_object(&class_name);
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
        luaL_error(L, METHOD_NOT_BUILT_ERR, p_class.name, p_method.name);

    luaGD_checkpermissions(L, p_method.debug_name, get_method_permissions(p_class, p_method));

    LocalVector<Variant> varargs;
    LocalVector<LuauVariant> args;
    LocalVector<const void *> pargs;

    get_arguments<ApiClassMethod, ApiClassArgument>(L, p_method.name, &varargs, &args, &pargs, p_method);

    GDExtensionObjectPtr self = nullptr;

    if (!p_method.is_static) {
        LuauVariant self_var;
        self_var.lua_check(L, 1, GDEXTENSION_VARIANT_TYPE_OBJECT, p_class.name);

        self = *self_var.get_ptr<GDExtensionObjectPtr>();
    }

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
        luaL_error(L, REFCOUNTED_FREE_ERR);

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

    luaL_error(L, SETGET_NOT_FOUND_ERR, p_method.utf8().get_data());
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
                luaL_error(L, PROP_GET_FAILED_PREV_ERR, key);
            } else {
                luaL_error(L, PROP_GET_FAILED_UNK_ERR, key); // due to the checks above, this should hopefully never happen
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
                luaL_error(L, PROP_SET_FAILED_PREV_ERR, key);
            } else {
                luaL_error(L, PROP_SET_FAILED_UNK_ERR, key); // should never happen
            }
        } else if (inst->get_signal(key)) {
            luaL_error(L, SIGNAL_READ_ONLY_ERR, key);
        } else if (inst->get_constant(key)) {
            luaGD_constassignerror(L, key);
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
            luaL_error(L, SIGNAL_READ_ONLY_ERR, key);

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

        // All methods (global table)
        for (const KeyValue<String, ApiClassMethod> &pair : g_class.methods) {
            push_class_method(L, g_class, pair.value);
            lua_setfield(L, -2, pair.value.name);
        }

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

/////////////
// Globals //
/////////////

static int luaGD_utility_function(lua_State *L) {
    const ApiUtilityFunction *func = luaGD_lightudataup<ApiUtilityFunction>(L, 1);

    LocalVector<Variant> varargs;
    LocalVector<LuauVariant> args;
    LocalVector<const void *> pargs;

    int nargs = get_arguments<ApiUtilityFunction, ApiArgumentNoDefault>(L, func->name, &varargs, &args, &pargs, *func);

    if (func->return_type == -1) {
        SET_CALL_STACK(L);
        func->func(nullptr, pargs.ptr(), nargs);
        CLEAR_CALL_STACK;
        return 0;
    } else {
        LuauVariant ret;
        ret.initialize((GDExtensionVariantType)func->return_type);

        SET_CALL_STACK(L);
        func->func(ret.get_opaque_pointer(), pargs.ptr(), nargs);
        CLEAR_CALL_STACK;

        ret.lua_push(L);
        return 1;
    }
}

static int luaGD_print_function(lua_State *L) {
    GDExtensionPtrUtilityFunction func = (GDExtensionPtrUtilityFunction)lua_tolightuserdata(L, lua_upvalueindex(1));

    int nargs = lua_gettop(L);

    LocalVector<Variant> varargs;
    LocalVector<const void *> pargs;

    varargs.resize(nargs);
    pargs.resize(nargs);

    for (int i = 0; i < nargs; i++) {
        if (LuaStackOp<Variant>::is(L, i + 1)) {
            varargs[i] = LuaStackOp<Variant>::get(L, i + 1);
        } else {
            varargs[i] = luaL_tolstring(L, i + 1, nullptr);
        }

        pargs[i] = &varargs[i];
    }

    func(nullptr, pargs.ptr(), pargs.size());
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

    push_enum(L, get_pck_scan_error_enum());
    lua_setfield(L, -2, get_pck_scan_error_enum().name);

    push_enum(L, get_pck_file_scan_error_enum());
    lua_setfield(L, -2, get_pck_file_scan_error_enum().name);

    push_enum(L, get_sandbox_violations_enum());
    lua_setfield(L, -2, get_sandbox_violations_enum().name);

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
