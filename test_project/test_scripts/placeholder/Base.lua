local Base = gdclass({ extends = "Node" })

--@1

Base:RegisterProperty("baseProperty", {
    property = gdproperty({ type = Enum.VariantType.STRING }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = "hello"
})

return Base
