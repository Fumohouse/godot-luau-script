local Base = gdclass({ extends = "Node" })

--@1

Base:RegisterProperty("baseProperty", {
    property = gdproperty({ type = Enum.VariantType.STRING }),
    default = "hello"
})

return Base
