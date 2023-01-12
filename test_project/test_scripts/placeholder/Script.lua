local Script = gdclass(nil, "Base.lua")

--@1

Script:RegisterProperty("testProperty", { type = Enum.VariantType.FLOAT })
    :Default(4.25)

return Script
