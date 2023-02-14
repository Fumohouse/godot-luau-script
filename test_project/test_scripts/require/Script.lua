local Base = require("Base")
--@1

local Script = gdclass(nil, "Base.lua")

Script:RegisterProperty("baseMsg", Enum.VariantType.STRING)
    :Default(Base.TestFunc())

Script:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :Default(4.25)

return Script
