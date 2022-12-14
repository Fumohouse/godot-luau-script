#include "luagd_variant.h"

#include <gdextension_interface.h>
#include <lua.h>
#include <lualib.h>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luagd_bindings_stack.gen.h"
#include "luagd_stack.h"

// also, this class is easily the most demented thing in this entire project.
// beware of random SIGINTs and SIGSEGVs out of nowhere. :)

typedef void *(*LuauVariantGetPtr)(LuauVariant &self, bool is_arg);
typedef const void *(*LuauVariantGetPtrConst)(const LuauVariant &self);
typedef void (*LuauVariantNoRet)(LuauVariant &self);
typedef bool (*LuauVariantLuaIs)(lua_State *L, int idx, const String &type_name, GDExtensionVariantType typed_array_type);
typedef bool (*LuauVariantLuaCheck)(LuauVariant &self, lua_State *L, int idx, const String &type_name, GDExtensionVariantType typed_array_type);
typedef void (*LuauVariantLuaPush)(LuauVariant &self, lua_State *L);
typedef void (*LuauVariantCopy)(LuauVariant &self, const LuauVariant &other);
typedef void (*LuauVariantAssign)(LuauVariant &self, const Variant &val);

// for sanity reasons, methods defined for this struct
// should never, ever, ever, ever, set the variant's type or whether it was from Luau.
// otherwise, suffer initialization hell.
struct VariantTypeMethods {
    bool checks_luau_ptr;

    LuauVariantGetPtr get_ptr;
    LuauVariantGetPtrConst get_ptr_const;
    LuauVariantNoRet initialize;
    LuauVariantLuaIs lua_is;
    LuauVariantLuaCheck lua_check;
    LuauVariantLuaPush lua_push;
    LuauVariantNoRet destroy;
    LuauVariantCopy copy;
    LuauVariantAssign assign;
};

static void no_op(LuauVariant &self) {}

#define GETTER(getter_name) [](LuauVariant &self, bool) -> void * { return self.getter_name(); }
#define GETTER_CONST(getter_name) [](const LuauVariant &self) -> const void * { return self.getter_name(); }

#define SIMPLE_METHODS(type, get, union_name)                                                          \
    {                                                                                                  \
        false,                                                                                         \
                GETTER(get),                                                                           \
                GETTER_CONST(get),                                                                     \
                no_op,                                                                                 \
                [](lua_State *L, int idx, const String &, GDExtensionVariantType) {                    \
                    return LuaStackOp<type>::is(L, idx);                                               \
                },                                                                                     \
                [](LuauVariant &self, lua_State *L, int idx, const String &, GDExtensionVariantType) { \
                    self._data.union_name = LuaStackOp<type>::check(L, idx);                           \
                    return true;                                                                       \
                },                                                                                     \
                [](LuauVariant &self, lua_State *L) {                                                  \
                    LuaStackOp<type>::push(L, self._data.union_name);                                  \
                },                                                                                     \
                no_op,                                                                                 \
                [](LuauVariant &self, const LuauVariant &other) {                                      \
                    self._data.union_name = other._data.union_name;                                    \
                },                                                                                     \
                [](LuauVariant &self, const Variant &val) {                                            \
                    self._data.union_name = val;                                                       \
                }                                                                                      \
    }

#define DATA_INIT(type) [](LuauVariant &self) { memnew_placement(self._data._data, type); }
#define DATA_PUSH(type) [](LuauVariant &self, lua_State *L) { LuaStackOp<type>::push(L, *((type *)self._data._data)); }
#define DATA_DTOR(type) [](LuauVariant &self) { ((type *)self._data._data)->~type(); }
#define DATA_COPY(p_type)                                                   \
    [](LuauVariant &self, const LuauVariant &other) {                       \
        if (other.is_from_luau())                                           \
            self._data._ptr = other._data._ptr;                             \
        else                                                                \
            *((p_type *)self._data._data) = *((p_type *)other._data._data); \
    }
#define DATA_ASSIGN(type)                       \
    [](LuauVariant &self, const Variant &val) { \
        if (self.is_from_luau())                \
            *((type *)self._data._ptr) = val;   \
        else                                    \
            *((type *)self._data._data) = val;  \
    }

#define DATA_METHODS(type, get)                                                                        \
    {                                                                                                  \
        true,                                                                                          \
                GETTER(get),                                                                           \
                GETTER_CONST(get),                                                                     \
                DATA_INIT(type),                                                                       \
                [](lua_State *L, int idx, const String &, GDExtensionVariantType) {                    \
                    return LuaStackOp<type>::is(L, idx);                                               \
                },                                                                                     \
                [](LuauVariant &self, lua_State *L, int idx, const String &, GDExtensionVariantType) { \
                    self._data._ptr = LuaStackOp<type>::check_ptr(L, idx);                             \
                    return true;                                                                       \
                },                                                                                     \
                DATA_PUSH(type),                                                                       \
                DATA_DTOR(type),                                                                       \
                DATA_COPY(type),                                                                       \
                DATA_ASSIGN(type)                                                                      \
    }

#define PTR_INIT(type, union_name) [](LuauVariant &self) { self._data.union_name = memnew(type); }
#define PTR_PUSH(type, union_name) [](LuauVariant &self, lua_State *L) { LuaStackOp<type>::push(L, *self._data.union_name); }
#define PTR_DTOR(type, union_name) [](LuauVariant &self) { memdelete(self._data.union_name); }
#define PTR_COPY(union_name)                                  \
    [](LuauVariant &self, const LuauVariant &other) {         \
        if (other.is_from_luau())                             \
            self._data._ptr = other._data._ptr;               \
        else                                                  \
            *self._data.union_name = *other._data.union_name; \
    }
#define PTR_ASSIGN(type, union_name)            \
    [](LuauVariant &self, const Variant &val) { \
        if (self.is_from_luau())                \
            *((type *)self._data._ptr) = val;   \
        else                                    \
            *self._data.union_name = val;       \
    }

#define PTR_METHODS(type, get, union_name)                                                             \
    {                                                                                                  \
        true,                                                                                          \
                GETTER(get),                                                                           \
                GETTER_CONST(get),                                                                     \
                PTR_INIT(type, union_name),                                                            \
                [](lua_State *L, int idx, const String &, GDExtensionVariantType) {                    \
                    return LuaStackOp<type>::is(L, idx);                                               \
                },                                                                                     \
                [](LuauVariant &self, lua_State *L, int idx, const String &, GDExtensionVariantType) { \
                    self._data._ptr = LuaStackOp<type>::check_ptr(L, idx);                             \
                    return true;                                                                       \
                },                                                                                     \
                PTR_PUSH(type, union_name),                                                            \
                PTR_DTOR(type, union_name),                                                            \
                PTR_COPY(union_name),                                                                  \
                PTR_ASSIGN(type, union_name)                                                           \
    }

static VariantTypeMethods type_methods[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = {
    // Variant has no check_ptr
    { false,
            GETTER(get_variant),
            GETTER_CONST(get_variant),
            PTR_INIT(Variant, _variant),
            [](lua_State *L, int idx, const String &, GDExtensionVariantType) {
                return LuaStackOp<Variant>::is(L, idx);
            },
            [](LuauVariant &self, lua_State *L, int idx, const String &, GDExtensionVariantType) {
                *self._data._variant = LuaStackOp<Variant>::check(L, idx);
                return false;
            },
            PTR_PUSH(Variant, _variant),
            PTR_DTOR(Variant, _variant),
            PTR_COPY(_variant),
            PTR_ASSIGN(Variant, _variant) },

    SIMPLE_METHODS(bool, get_bool, _bool),
    SIMPLE_METHODS(int64_t, get_int, _int),
    SIMPLE_METHODS(double, get_float, _float),

    // String is not bound to Godot
    { false,
            GETTER(get_string),
            GETTER_CONST(get_string),
            DATA_INIT(String),
            [](lua_State *L, int idx, const String &, GDExtensionVariantType) -> bool {
                return lua_isstring(L, idx);
            },
            [](LuauVariant &self, lua_State *L, int idx, const String &, GDExtensionVariantType) {
                *((String *)self._data._data) = LuaStackOp<String>::check(L, idx);
                return false;
            },
            DATA_PUSH(String),
            DATA_DTOR(String),
            DATA_COPY(String),
            DATA_ASSIGN(String) },

    DATA_METHODS(Vector2, get_vector2),
    DATA_METHODS(Vector2i, get_vector2),
    DATA_METHODS(Rect2, get_rect2),
    DATA_METHODS(Rect2i, get_rect2i),
    DATA_METHODS(Vector3, get_vector3),
    DATA_METHODS(Vector3i, get_vector3i),
    PTR_METHODS(Transform2D, get_transform2d, _transform2d),
    DATA_METHODS(Vector4, get_vector4),
    DATA_METHODS(Vector4i, get_vector4i),
    DATA_METHODS(Plane, get_plane),
    DATA_METHODS(Quaternion, get_quaternion),
    PTR_METHODS(AABB, get_aabb, _aabb),
    PTR_METHODS(Basis, get_basis, _basis),
    PTR_METHODS(Transform3D, get_transform3d, _transform3d),
    PTR_METHODS(Projection, get_projection, _projection),

    DATA_METHODS(Color, get_color),
    DATA_METHODS(StringName, get_string_name),
    DATA_METHODS(NodePath, get_node_path),
    DATA_METHODS(RID, get_rid),
    { false,
            [](LuauVariant &self, bool is_arg) -> void *{
                if (is_arg)
                    return self.get_object_arg();
                else
                    return self.get_object();
            },
            GETTER_CONST(get_object),
            [](LuauVariant &self) {
                self._data._object = nullptr;
            },
            [](lua_State *L, int idx, const String &type_name, GDExtensionVariantType) {
                Object *obj = LuaStackOp<Object *>::get(L, idx);
                if (obj == nullptr)
                    return false;

                return type_name.is_empty() || obj->is_class(type_name);
            },
            [](LuauVariant &self, lua_State *L, int idx, const String &type_name, GDExtensionVariantType) {
                Object *obj = LuaStackOp<Object *>::check(L, idx);
                if (obj == nullptr)
                    luaL_error(L, "Object has been freed");

                if (!type_name.is_empty() && !obj->is_class(type_name))
                    luaL_typeerrorL(L, idx, type_name.utf8().get_data());

                self._data._object = obj->_owner;
                return false;
            },
            [](LuauVariant &self, lua_State *L) {
                Object *inst = ObjectDB::get_instance(internal::gde_interface->object_get_instance_id(self._data._object));
                LuaStackOp<Object *>::push(L, inst);
            },
            no_op,
            [](LuauVariant &self, const LuauVariant &other) {
                self._data._object = other._data._object;
            },
            [](LuauVariant &self, const Variant &val) {
                self._data._object = val;
            } },
    DATA_METHODS(Callable, get_callable),
    DATA_METHODS(Signal, get_signal),
    DATA_METHODS(Dictionary, get_dictionary),
    { true,
            GETTER(get_array),
            GETTER_CONST(get_array),
            DATA_INIT(Array),
            [](lua_State *L, int idx, const String &type_name, GDExtensionVariantType typed_array_type) {
                return LuaStackOp<Array>::is(L, idx, (Variant::Type)typed_array_type, type_name);
            },
            [](LuauVariant &self, lua_State *L, int idx, const String &type_name, GDExtensionVariantType typed_array_type) {
                if (Array *ptr = LuaStackOp<Array>::get_ptr(L, idx)) {
                    self._data._ptr = ptr;
                    return true;
                }

                *(Array *)self._data._data = LuaStackOp<Array>::check(L, idx);
                return false;
            },
            DATA_PUSH(Array),
            DATA_DTOR(Array),
            DATA_COPY(Array),
            DATA_ASSIGN(Array) },

    DATA_METHODS(PackedByteArray, get_byte_array),
    DATA_METHODS(PackedInt32Array, get_int32_array),
    DATA_METHODS(PackedInt64Array, get_int64_array),
    DATA_METHODS(PackedFloat32Array, get_float32_array),
    DATA_METHODS(PackedFloat64Array, get_float64_array),
    DATA_METHODS(PackedStringArray, get_string_array),
    DATA_METHODS(PackedVector2Array, get_vector2_array),
    DATA_METHODS(PackedVector3Array, get_vector3_array),
    DATA_METHODS(PackedColorArray, get_color_array)
};

void *LuauVariant::get_opaque_pointer_arg() {
    return type_methods[type].get_ptr(*this, true);
}

void *LuauVariant::get_opaque_pointer() {
    return type_methods[type].get_ptr(*this, false);
}

const void *LuauVariant::get_opaque_pointer() const {
    return type_methods[type].get_ptr_const(*this);
}

void LuauVariant::initialize(GDExtensionVariantType init_type) {
    clear();
    type_methods[init_type].initialize(*this);
    type = init_type;
}

bool LuauVariant::lua_is(lua_State *L, int idx, GDExtensionVariantType required_type, const String &type_name, GDExtensionVariantType typed_array_type) {
    return type_methods[required_type].lua_is(L, idx, type_name, typed_array_type);
}

void LuauVariant::lua_check(lua_State *L, int idx, GDExtensionVariantType required_type, const String &type_name, GDExtensionVariantType typed_array_type) {
    const VariantTypeMethods &methods = type_methods[required_type];

    if (methods.checks_luau_ptr) {
        type = required_type;
    } else {
        initialize(required_type);
    }

    from_luau = methods.lua_check(*this, L, idx, type_name, typed_array_type);
}

void LuauVariant::lua_push(lua_State *L) {
    ERR_FAIL_COND_MSG(from_luau, "pushing a value from Luau back to Luau is unsupported");
    type_methods[type].lua_push(*this, L);
}

void LuauVariant::assign_variant(const Variant &val) {
    if (type == -1)
        return;

    type_methods[type].assign(*this, val);
}

void LuauVariant::clear() {
    if (type == -1)
        return;

    if (!from_luau)
        type_methods[type].destroy(*this);

    type = -1;
}

void LuauVariant::copy_variant(LuauVariant &to, const LuauVariant &from) {
    if (from.type != -1) {
        if (!from.from_luau)
            to.initialize((GDExtensionVariantType)from.type);

        type_methods[from.type].copy(to, from);
    }

    to.type = from.type;
    to.from_luau = from.from_luau;
}

LuauVariant::LuauVariant(const LuauVariant &from) {
    copy_variant(*this, from);
}

LuauVariant &LuauVariant::operator=(const LuauVariant &from) {
    if (this == &from)
        return *this;

    copy_variant(*this, from);
    return *this;
}

LuauVariant::~LuauVariant() {
    clear();
}