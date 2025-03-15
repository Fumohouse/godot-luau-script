# ! SYNC WITH Variant::Type
# ! https://github.com/godotengine/godot/blob/master/core/variant/variant.h
VariantType = [
    "Variant",
    "bool",
    "int",
    "float",
    "String",
    "Vector2",
    "Vector2i",
    "Rect2",
    "Rect2i",
    "Vector3",
    "Vector3i",
    "Transform2D",
    "Vector4",
    "Vector4i",
    "Plane",
    "Quaternion",
    "AABB",
    "Basis",
    "Transform3D",
    "Projection",
    "Color",
    "StringName",
    "NodePath",
    "RID",
    "Object",
    "Callable",
    "Signal",
    "Dictionary",
    "Array",
    "PackedByteArray",
    "PackedInt32Array",
    "PackedInt64Array",
    "PackedFloat32Array",
    "PackedFloat64Array",
    "PackedStringArray",
    "PackedVector2Array",
    "PackedVector3Array",
    "PackedColorArray",
    "PackedVector4Array",
]

# ! SYNC WITH Variant::Operator
# ! core/variant/variant_op.cpp
VariantOperator = [
    "==",
    "!=",
    "<",
    "<=",
    ">",
    ">=",
    "+",
    "-",
    "*",
    "/",
    "unary-",
    "unary+",
    "%",
    "**",
    "<<",
    ">>",
    "&",
    "|",
    "^",
    "~",
    "and",
    "or",
    "xor",
    "not",
    "in",
]


def get_variant_type(type_name):
    return VariantType.index(type_name)


def get_variant_op(op_name):
    return VariantOperator.index(op_name)
