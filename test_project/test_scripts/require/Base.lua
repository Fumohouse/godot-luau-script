local Module = require("Module.mod")
--@1

--- @class
--- @extends Node
local Base = {}
local BaseC = gdclass(Base)

--- @classType Base
export type Base = Node & typeof(Base)

--- @registerConstant
Base.TEST_CONSTANT = Module.kTestConstantValue

function Base.TestFunc()
    return "what's up"
end

return BaseC
