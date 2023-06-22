local Module = require("Module.mod")

--- @class
--- @extends Node
local Base = {}
local BaseC = gdclass(Base)

--- @classType Base
export type Base = Node & typeof(Base) & {
    --- @property
    baseProperty: string,

    --@1
}

--- @registerConstant
Base.TEST_CONSTANT = Module.kTestConstantValue

return BaseC
