local Script = gdclass({ extends = "Base.lua" })

--@1

Script:RegisterProperty("testProperty", {
    property = gdproperty({ type = Enum.VariantType.FLOAT }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = 4.25
})

return Script
