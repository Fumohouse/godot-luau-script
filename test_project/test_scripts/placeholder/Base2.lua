local Base2 = gdclass({ extends = "Node" })

--@1

Base2:RegisterProperty("baseProperty2", {
    property = gdproperty({ type = Enum.VariantType.VECTOR2 }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = Vector2(1, 2)
})

return Base2
