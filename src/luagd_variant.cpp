#include "luagd_variant.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <lua.h>
#include <lualib.h>

#include "luagd_stack.h"
#include "luagd_bindings_stack.gen.h"

// the formatter i use seriously hates this file. enjoy the madness

typedef void (*LuauVariantNoRet)(LuauVariant &self);
typedef void (*LuauVariantLuaCheck)(LuauVariant &self, lua_State *L, int idx, String type_name);
typedef void (*LuauVariantLuaPush)(LuauVariant &self, lua_State *L);
typedef void *(*LuauVariantGetPtr)(LuauVariant &self);
typedef const void *(*LuauVariantGetPtrConst)(const LuauVariant &self);
typedef void (*LuauVariantCopy)(LuauVariant &self, const LuauVariant &other);
typedef void (*LuauVariantAssign)(LuauVariant &self, const Variant &val);

struct VariantTypeMethods
{
    LuauVariantGetPtr get_ptr;
    LuauVariantGetPtrConst get_ptr_const;
    LuauVariantNoRet initialize;
    LuauVariantLuaCheck lua_check;
    LuauVariantLuaPush lua_push;
    LuauVariantNoRet destroy;
    LuauVariantCopy copy;
    LuauVariantAssign assign;
};

static void no_op(LuauVariant &self) {}

#define GETTER(getter_name) [](LuauVariant &self) -> void * { return self.getter_name(); }
#define GETTER_CONST(getter_name) [](const LuauVariant &self) -> const void * { return self.getter_name(); }

#define SIMPLE_METHODS(type, get, union_name)                            \
    {                                                                    \
        GETTER(get),                                                     \
            GETTER_CONST(get),                                           \
            no_op,                                                       \
            [](LuauVariant &self, lua_State *L, int idx, String) {       \
                self._data.union_name = LuaStackOp<type>::check(L, idx); \
            },                                                           \
            [](LuauVariant &self, lua_State *L) {                        \
                LuaStackOp<type>::push(L, self._data.union_name);        \
            },                                                           \
            no_op,                                                       \
            [](LuauVariant &self, const LuauVariant &other) {            \
                self._data.union_name = other._data.union_name;          \
            },                                                           \
            [](LuauVariant &self, const Variant &val) {                  \
                self._data.union_name = val;                             \
            }                                                            \
    }

#define DATA_INIT(type) [](LuauVariant &self) { memnew_placement(self._data._data, type); }
#define DATA_PUSH(type) [](LuauVariant &self, lua_State *L) { LuaStackOp<type>::push(L, *((type *)self._data._data)); }
#define DATA_DTOR(type) [](LuauVariant &self) { ((type *)self._data._data)->~type(); }
#define DATA_COPY(p_type)                                               \
    [](LuauVariant &self, const LuauVariant &other)                     \
    {                                                                   \
        if (other.from_luau)                                            \
        {                                                               \
            self._data._ptr = other._data._ptr;                         \
            self.from_luau = true;                                      \
            return;                                                     \
        }                                                               \
                                                                        \
        self.initialize((GDExtensionVariantType)other.type);            \
        *((p_type *)self._data._data) = *((p_type *)other._data._data); \
    }
#define DATA_ASSIGN(type)                      \
    [](LuauVariant &self, const Variant &val)  \
    {                                          \
        if (self.from_luau)                    \
            *((type *)self._data._ptr) = val;  \
        else                                   \
            *((type *)self._data._data) = val; \
    }

#define DATA_METHODS(type, get)                                        \
    {                                                                  \
        GETTER(get),                                                   \
            GETTER_CONST(get),                                         \
            DATA_INIT(type),                                           \
            [](LuauVariant &self, lua_State *L, int idx, String) {     \
                self._data._ptr = LuaStackOp<type>::check_ptr(L, idx); \
                self.from_luau = true;                                 \
            },                                                         \
            DATA_PUSH(type),                                           \
            DATA_DTOR(type),                                           \
            DATA_COPY(type),                                           \
            DATA_ASSIGN(type)                                          \
    }

#define PTR_INIT(type, union_name) [](LuauVariant &self) { self._data.union_name = memnew(type); }
#define PTR_PUSH(type, union_name) [](LuauVariant &self, lua_State *L) { LuaStackOp<type>::push(L, *self._data.union_name); }
#define PTR_DTOR(type, union_name) [](LuauVariant &self) { memdelete(self._data.union_name); }
#define PTR_COPY(union_name)                                 \
    [](LuauVariant &self, const LuauVariant &other)          \
    {                                                        \
        if (other.from_luau)                                 \
        {                                                    \
            self._data._ptr = other._data._ptr;              \
            self.from_luau = true;                           \
            return;                                          \
        }                                                    \
                                                             \
        self.initialize((GDExtensionVariantType)other.type); \
        *self._data.union_name = *other._data.union_name;    \
    }
#define PTR_ASSIGN(type, union_name)          \
    [](LuauVariant &self, const Variant &val) \
    {                                         \
        if (self.from_luau)                   \
            *((type *)self._data._ptr) = val; \
        else                                  \
            *self._data.union_name = val;     \
    }

#define PTR_METHODS(type, get, union_name)                             \
    {                                                                  \
        GETTER(get),                                                   \
            GETTER_CONST(get),                                         \
            PTR_INIT(type, union_name),                                \
            [](LuauVariant &self, lua_State *L, int idx, String) {     \
                self._data._ptr = LuaStackOp<type>::check_ptr(L, idx); \
                self.from_luau = true;                                 \
            },                                                         \
            PTR_PUSH(type, union_name),                                \
            PTR_DTOR(type, union_name),                                \
            PTR_COPY(union_name),                                      \
            PTR_ASSIGN(type, union_name)                               \
    }

static VariantTypeMethods type_methods[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = {
    // Variant has no check_ptr
    {GETTER(get_variant),
     GETTER_CONST(get_variant),
     PTR_INIT(Variant, _variant),
     [](LuauVariant &self, lua_State *L, int idx, String)
     {
         self.initialize(GDEXTENSION_VARIANT_TYPE_NIL);
         *self._data._variant = LuaStackOp<Variant>::check(L, idx);
     },
     PTR_PUSH(Variant, _variant),
     PTR_DTOR(Variant, _variant),
     PTR_COPY(_variant),
     PTR_ASSIGN(Variant, _variant)},

    SIMPLE_METHODS(bool, get_bool, _bool),
    SIMPLE_METHODS(int64_t, get_int, _int),
    SIMPLE_METHODS(double, get_float, _float),

    // String is not bound to Godot
    {GETTER(get_string),
     GETTER_CONST(get_string),
     DATA_INIT(String),
     [](LuauVariant &self, lua_State *L, int idx, String)
     {
         self.initialize(GDEXTENSION_VARIANT_TYPE_STRING);
         *((String *)self._data._data) = LuaStackOp<String>::check(L, idx);
     },
     DATA_PUSH(String),
     DATA_DTOR(String),
     DATA_COPY(String),
     DATA_ASSIGN(String)},

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
    {GETTER(get_object),
     GETTER_CONST(get_object),
     no_op,
     [](LuauVariant &self, lua_State *L, int idx, String type_name)
     {
         Object *obj = LuaStackOp<Object *>::check(L, idx);
         if (obj == nullptr)
             luaL_error(L, "Object has been freed");

         if (!obj->is_class(type_name))
             luaL_typeerrorL(L, idx, type_name.utf8().get_data());

         self._data._object = obj->_owner;
     },
     [](LuauVariant &self, lua_State *L)
     {
         Object *inst = ObjectDB::get_instance(internal::gde_interface->object_get_instance_id(self._data._object));
         LuaStackOp<Object *>::push(L, inst);
     },
     no_op,
     [](LuauVariant &self, const LuauVariant &other)
     {
         self._data._object = other._data._object;
     },
     [](LuauVariant &self, const Variant &val)
     {
         self._data._object = val;
     }},
    DATA_METHODS(Callable, get_callable),
    DATA_METHODS(Signal, get_signal),
    DATA_METHODS(Dictionary, get_dictionary),
    DATA_METHODS(Array, get_array),

    DATA_METHODS(PackedByteArray, get_byte_array),
    DATA_METHODS(PackedInt32Array, get_int32_array),
    DATA_METHODS(PackedInt64Array, get_int64_array),
    DATA_METHODS(PackedFloat32Array, get_float32_array),
    DATA_METHODS(PackedFloat64Array, get_float64_array),
    DATA_METHODS(PackedStringArray, get_string_array),
    DATA_METHODS(PackedVector2Array, get_vector2_array),
    DATA_METHODS(PackedVector3Array, get_vector3_array),
    DATA_METHODS(PackedColorArray, get_color_array)};

void *LuauVariant::get_opaque_pointer()
{
    return type_methods[type].get_ptr(*this);
}

const void *LuauVariant::get_opaque_pointer() const
{
    return type_methods[type].get_ptr_const(*this);
}

void LuauVariant::initialize(GDExtensionVariantType init_type)
{
    clear();
    type = init_type;
    type_methods[init_type].initialize(*this);
}

void LuauVariant::lua_check(lua_State *L, int idx, GDExtensionVariantType required_type, String type_name)
{
    type_methods[required_type].lua_check(*this, L, idx, type_name);
    type = required_type;
}

void LuauVariant::lua_push(lua_State *L)
{
    ERR_FAIL_COND_MSG(from_luau, "pushing a value from Luau back to Luau is unsupported");
    type_methods[type].lua_push(*this, L);
}

void LuauVariant::assign_variant(const Variant &val)
{
    if (type == -1)
        return;

    type_methods[type].assign(*this, val);
}

void LuauVariant::clear()
{
    if (type == -1)
        return;

    if (!from_luau)
        type_methods[type].destroy(*this);

    type = -1;
}

static void copy_variant(LuauVariant &to, const LuauVariant &from)
{
    if (from.type != -1)
    {
        type_methods[from.type].copy(to, from);
        to.type = from.type;
    }
}

LuauVariant::LuauVariant(const LuauVariant &from)
{
    copy_variant(*this, from);
}

LuauVariant &LuauVariant::operator=(const LuauVariant &from)
{
    if (this == &from)
        return *this;

    copy_variant(*this, from);
    return *this;
}

LuauVariant::~LuauVariant()
{
    clear();
}