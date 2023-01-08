local Script = {
    extends = "Base.lua",
    properties = {},
}

--@1

Script.properties["testProperty"] = {
    property = gdproperty({ name = "testProperty", type = Enum.VariantType.FLOAT }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = 4.25
}

return Script
