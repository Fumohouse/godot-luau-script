#pragma once

#ifdef DEBUG_ENABLED
#include <lua.h>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

#include "luagd.h"

using namespace godot;

#define LUAU_TEST_STACK_OPS(type, name)   \
    void _push_##name(type value)         \
    {                                     \
        luaGD_push<type>(L, value);       \
    }                                     \
                                          \
    type _get_##name(int index)           \
    {                                     \
        return luaGD_get<type>(L, index); \
    }

// A test node for working with the Luau VM directly,
// instead of as a scripting language.

class LuauTest : public Node
{
    GDCLASS(LuauTest, Node);

private:
    lua_State *L;

protected:
    static void _bind_methods();

    LUAU_TEST_STACK_OPS(bool, boolean);
    LUAU_TEST_STACK_OPS(int, integer);
    LUAU_TEST_STACK_OPS(float, number);
    LUAU_TEST_STACK_OPS(String, string);

    // lol
    LUAU_TEST_STACK_OPS(Vector2, vector2);
    LUAU_TEST_STACK_OPS(Vector2i, vector2i);
    LUAU_TEST_STACK_OPS(Rect2, rect2);
    LUAU_TEST_STACK_OPS(Rect2i, rect2i);
    LUAU_TEST_STACK_OPS(Vector3, vector3);
    LUAU_TEST_STACK_OPS(Vector3i, vector3i);
    LUAU_TEST_STACK_OPS(Transform2D, transform2D);
    LUAU_TEST_STACK_OPS(Vector4, vector4);
    LUAU_TEST_STACK_OPS(Vector4i, vector4i);
    LUAU_TEST_STACK_OPS(Plane, plane);
    LUAU_TEST_STACK_OPS(Quaternion, quaternion);
    LUAU_TEST_STACK_OPS(AABB, aabb);
    LUAU_TEST_STACK_OPS(Basis, basis);
    LUAU_TEST_STACK_OPS(Transform3D, transform3D);
    LUAU_TEST_STACK_OPS(Projection, projection);
    LUAU_TEST_STACK_OPS(Color, color);
    LUAU_TEST_STACK_OPS(StringName, string_name);
    LUAU_TEST_STACK_OPS(NodePath, node_path);
    LUAU_TEST_STACK_OPS(RID, rid);
    LUAU_TEST_STACK_OPS(Callable, callable);
    LUAU_TEST_STACK_OPS(Signal, signal);
    LUAU_TEST_STACK_OPS(Dictionary, dictionary);
    LUAU_TEST_STACK_OPS(Array, array);
    LUAU_TEST_STACK_OPS(PackedByteArray, packed_byte_array);
    LUAU_TEST_STACK_OPS(PackedInt32Array, packed_int32_array);
    LUAU_TEST_STACK_OPS(PackedInt64Array, packed_int64_array);
    LUAU_TEST_STACK_OPS(PackedFloat32Array, packed_float32_array);
    LUAU_TEST_STACK_OPS(PackedFloat64Array, packed_float64_array);
    LUAU_TEST_STACK_OPS(PackedStringArray, packed_string_array);
    LUAU_TEST_STACK_OPS(PackedVector2Array, packed_vector2_array);
    LUAU_TEST_STACK_OPS(PackedVector3Array, packed_vector3_array);
    LUAU_TEST_STACK_OPS(PackedColorArray, packed_color_array);

    LUAU_TEST_STACK_OPS(Variant, variant);
    LUAU_TEST_STACK_OPS(Object *, object);

    void _set_top(int index);
    bool _gc_collect();
    Dictionary _exec(String source);
    void _set_global(String key);

public:
    LuauTest();
    ~LuauTest();
};
#endif // DEBUG_ENABLED
