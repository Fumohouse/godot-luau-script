local Base2 = {
    extends = "Node",
    properties = {},
}

--@1

Base2.properties["baseProperty2"] = {
    property = gdproperty({ name = "baseProperty2", type = Enum.VariantType.VECTOR2 }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = Vector2(1, 2)
}

return Base2
