local Base = gdclass(nil, "Node")

--@1

Base:RegisterProperty("baseProperty", { type = Enum.VariantType.STRING })
    :Default("hello")

return Base
