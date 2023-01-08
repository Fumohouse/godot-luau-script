local Base = {
    extends = "Node",
    properties = {},
}

--@1

Base.properties["baseProperty"] = {
    property = gdproperty({ name = "baseProperty", type = Enum.VariantType.STRING }),
	usage = Enum.PropertyUsageFlags.STORAGE,
    default = "hello"
}

return Base
