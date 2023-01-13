#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;

struct lua_State;

// basically Variant + VariantInternal. ended up being pretty much the same.
// see README.md for Godot Engine license

#define DATA_SIZE sizeof(real_t) * 4

#define SIMPLE_GETTER(type, method_name, union_name)       \
    _FORCE_INLINE_ type *get_##method_name() {             \
        return &_data.union_name;                          \
    }                                                      \
                                                           \
    _FORCE_INLINE_ const type *get_##method_name() const { \
        return &_data.union_name;                          \
    }

#define DATA_GETTER(type, method_name)                                                             \
    static_assert(sizeof(type) <= DATA_SIZE, "this type should not use the Variant _data buffer"); \
                                                                                                   \
    _FORCE_INLINE_ type *get_##method_name() {                                                     \
        if (from_luau)                                                                             \
            return reinterpret_cast<type *>(_data._ptr);                                           \
                                                                                                   \
        return reinterpret_cast<type *>(_data._data);                                              \
    }                                                                                              \
                                                                                                   \
    _FORCE_INLINE_ const type *get_##method_name() const {                                         \
        if (from_luau)                                                                             \
            return reinterpret_cast<type *>(_data._ptr);                                           \
                                                                                                   \
        return reinterpret_cast<const type *>(_data._data);                                        \
    }

#define PTR_GETTER(type, method_name, ptr_name)            \
    _FORCE_INLINE_ type *get_##method_name() {             \
        if (from_luau)                                     \
            return reinterpret_cast<type *>(_data._ptr);   \
                                                           \
        return _data.ptr_name;                             \
    }                                                      \
                                                           \
    _FORCE_INLINE_ const type *get_##method_name() const { \
        if (from_luau)                                     \
            return reinterpret_cast<type *>(_data._ptr);   \
                                                           \
        return _data.ptr_name;                             \
    }

class LuauVariant // jank
{
private:
    int32_t type;

    // forces the value to refer to a Luau userdata pointer
    // limits lifetime based on Luau GC (this type should be temporary anyway)
    bool from_luau;

public:
    // ! union is public for reducing jank. don't touch it.
    // size is 16 bytes. anything under can be added easily, otherwise use ptr
    union U {
        constexpr U() :
                _bool(false) {} // https://stackoverflow.com/a/70428826
        ~U() noexcept {}

        // SIMPLE
        bool _bool;
        int64_t _int;
        double _float;
        GDExtensionObjectPtr _object;

        // DATA
        uint8_t _data[DATA_SIZE];

        // PTR
        Variant *_variant; // variant ception
        Transform2D *_transform2d;
        Transform3D *_transform3d;
        Projection *_projection;
        AABB *_aabb;
        Basis *_basis;

        // Luau
        void *_ptr;
    } _data alignas(8);

    /* Opaque pointer */
    SIMPLE_GETTER(bool, bool, _bool)
    SIMPLE_GETTER(int64_t, int, _int)
    SIMPLE_GETTER(double, float, _float)
    DATA_GETTER(String, string)

    DATA_GETTER(Vector2, vector2)
    DATA_GETTER(Vector2i, vector2i)
    DATA_GETTER(Rect2, rect2)
    DATA_GETTER(Rect2i, rect2i)
    DATA_GETTER(Vector3, vector3)
    DATA_GETTER(Vector3i, vector3i)
    PTR_GETTER(Transform2D, transform2d, _transform2d)
    DATA_GETTER(Vector4, vector4)
    DATA_GETTER(Vector4i, vector4i)
    DATA_GETTER(Plane, plane)
    DATA_GETTER(Quaternion, quaternion)
    PTR_GETTER(AABB, aabb, _aabb)
    PTR_GETTER(Basis, basis, _basis)
    PTR_GETTER(Projection, projection, _projection)
    PTR_GETTER(Transform3D, transform3d, _transform3d)

    DATA_GETTER(Color, color)
    DATA_GETTER(StringName, string_name)
    DATA_GETTER(NodePath, node_path)
    DATA_GETTER(RID, rid)

    SIMPLE_GETTER(GDExtensionObjectPtr, object, _object);

    // TODO: 2023-01-09: This may change. Tracking https://github.com/godotengine/godot/issues/61967.
    // Special case. Object pointers are treated as GodotObject * when passing to Godot,
    // and GodotObject ** when returning from Godot.

    // Current understanding of the situation:
    // - Use _arg for all arguments
    // - _arg is never needed for return values or builtin/class `self`
    // - _arg is never needed for values which will never be Object

    // This is a mess. Hopefully it gets fixed at some point.
    _FORCE_INLINE_ GDExtensionObjectPtr get_object_arg() {
        return _data._object;
    }

    DATA_GETTER(Callable, callable)
    DATA_GETTER(Signal, signal)
    DATA_GETTER(Dictionary, dictionary)
    DATA_GETTER(Array, array)

    DATA_GETTER(PackedByteArray, byte_array)
    DATA_GETTER(PackedInt32Array, int32_array)
    DATA_GETTER(PackedInt64Array, int64_array)
    DATA_GETTER(PackedFloat32Array, float32_array)
    DATA_GETTER(PackedFloat64Array, float64_array)
    DATA_GETTER(PackedStringArray, string_array)
    DATA_GETTER(PackedVector2Array, vector2_array)
    DATA_GETTER(PackedVector3Array, vector3_array)
    DATA_GETTER(PackedColorArray, color_array)

    PTR_GETTER(Variant, variant, _variant);

    void *get_opaque_pointer_arg();
    void *get_opaque_pointer();
    const void *get_opaque_pointer() const;

    /* Member getter */
    _FORCE_INLINE_ int32_t get_type() const { return type; }
    _FORCE_INLINE_ bool is_from_luau() const { return from_luau; }

    /* Initialization */
    void initialize(GDExtensionVariantType init_type);
    static bool lua_is(
            lua_State *L, int idx,
            GDExtensionVariantType required_type,
            const String &type_name = "",
            GDExtensionVariantType typed_array_type = GDEXTENSION_VARIANT_TYPE_NIL);
    void lua_check(
            lua_State *L, int idx,
            GDExtensionVariantType required_type,
            const String &type_name = "",
            GDExtensionVariantType typed_array_type = GDEXTENSION_VARIANT_TYPE_NIL);
    void lua_push(lua_State *L) const;

    /* Assignment */
    void assign_variant(const Variant &val);

    /* Constructor */
    _FORCE_INLINE_ LuauVariant() :
            type(-1), from_luau(false) {}
    LuauVariant(const LuauVariant &from);
    LuauVariant &operator=(const LuauVariant &from);
    ~LuauVariant();

private:
    void clear();
    static void copy_variant(LuauVariant &to, const LuauVariant &from);
};
