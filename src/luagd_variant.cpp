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
typedef void (*LuauVariantFromStack)(LuauVariant &self, lua_State *L, int idx, String class_name);
typedef void (*LuauVariantToStack)(LuauVariant &self, lua_State *L);
typedef void *(*LuauVariantGetPtr)(LuauVariant &self);
typedef const void *(*LuauVariantGetPtrConst)(const LuauVariant &self);

struct VariantTypeMethods
{
    LuauVariantGetPtr get_ptr;
    LuauVariantGetPtrConst get_ptr_const;
    LuauVariantNoRet initialize;
    LuauVariantFromStack from_stack;
    LuauVariantToStack to_stack;
    LuauVariantNoRet destroy;
};

static void no_op(LuauVariant &self) {}
static void no_op(LuauVariant &self, lua_State *L, int idx, String class_name) {}
static void no_op(LuauVariant &self, lua_State *L) {}

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
            no_op                                                        \
    }

#define DATA_METHODS(type, get)                                                \
    {                                                                          \
        GETTER(get),                                                           \
            GETTER_CONST(get),                                                 \
            [](LuauVariant &self) {                                            \
                memnew_placement(self._data._data, type);                      \
            },                                                                 \
            [](LuauVariant &self, lua_State *L, int idx, String) {             \
                *((type *)self._data._data) = LuaStackOp<type>::check(L, idx); \
            },                                                                 \
            [](LuauVariant &self, lua_State *L) {                              \
                LuaStackOp<type>::push(L, *((type *)self._data._data));        \
            },                                                                 \
            [](LuauVariant &self) {                                            \
                ((type *)self._data._data)->~type();                           \
            }                                                                  \
    }

#define PTR_METHODS(type, get, union_name)                                \
    {                                                                     \
        GETTER(get),                                                      \
            GETTER_CONST(get),                                            \
            [](LuauVariant &self) {                                       \
                self._data.union_name = memnew(type);                     \
            },                                                            \
            [](LuauVariant &self, lua_State *L, int idx, String) {        \
                *self._data.union_name = LuaStackOp<type>::check(L, idx); \
            },                                                            \
            [](LuauVariant &self, lua_State *L) {                         \
                LuaStackOp<type>::push(L, *self._data.union_name);        \
            },                                                            \
            [](LuauVariant &self) {                                       \
                memdelete(self._data.union_name);                         \
            }                                                             \
    }

static VariantTypeMethods type_methods[GDEXTENSION_VARIANT_TYPE_VARIANT_MAX] = {
    // get initialize from_stack destroy
    // NIL
    {
        [](LuauVariant &) -> void *
        { return nullptr; },
        [](const LuauVariant &) -> const void *
        { return nullptr; },
        no_op, no_op, no_op, no_op},

    SIMPLE_METHODS(bool, get_bool, _bool),
    SIMPLE_METHODS(int64_t, get_int, _int),
    SIMPLE_METHODS(double, get_float, _float),
    DATA_METHODS(String, get_string),

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
     [](LuauVariant &self, lua_State *L, int idx, String class_name)
     {
         Object *obj = LuaStackOp<Object *>::check(L, idx);
         if (obj != nullptr)
             luaL_error(L, "Object has been freed");

         if (obj->get_class() != class_name)
             luaL_typeerrorL(L, idx, class_name.utf8().get_data());

         self._data._object = obj->_owner;
     },
     [](LuauVariant &self, lua_State *L)
     {
         Object *inst = ObjectDB::get_instance(internal::gde_interface->object_get_instance_id(self._data._object));
         LuaStackOp<Object *>::push(L, inst);
     },
     no_op},
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
    type = init_type;
    type_methods[init_type].initialize(*this);
}

void LuauVariant::lua_check(lua_State *L, int idx, GDExtensionVariantType required_type, String class_name)
{
    initialize(required_type);
    type_methods[type].from_stack(*this, L, idx, class_name);
}

void LuauVariant::lua_push(lua_State *L)
{
    type_methods[type].to_stack(*this, L);
}

LuauVariant::~LuauVariant()
{
    type_methods[type].destroy(*this);
}