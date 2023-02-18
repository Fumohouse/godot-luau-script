local Module = require("Module.mod")
--@1

local BaseImpl = {}
local Base = gdclass(nil, Node)
    :RegisterImpl(BaseImpl)

function BaseImpl.TestFunc()
    return "what's up"
end

Base:RegisterProperty("baseProperty", Enum.VariantType.STRING)
    :Default(Module.kBasePropertyDefault)

return Base
