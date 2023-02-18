local Base = gdclass(nil, Node)

--@1

Base:RegisterProperty("baseProperty", Enum.VariantType.STRING)
    :Default("hello")

return Base
