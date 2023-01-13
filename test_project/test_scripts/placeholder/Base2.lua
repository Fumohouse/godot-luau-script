local Base2 = gdclass(nil, "Node")

--@1

Base2:RegisterProperty("baseProperty2", Enum.VariantType.VECTOR2)
    :Default(Vector2(1, 2))

return Base2
