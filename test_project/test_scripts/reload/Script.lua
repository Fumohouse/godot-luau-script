local Base = require("Base")

local Script = gdclass(nil, Base)

--@1

Script:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :Default(4.25)

return Script
