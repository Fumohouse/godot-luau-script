local Base = require("Base")
--@1

--- @class
--- @extends Base
local Script = {}
local ScriptC = gdclass(Script)

--- @classType Script
export type Script = Base.Base & typeof(Script) & {
    --- @property
    --- @default 4.25
    testProperty: number,
}

--- @registerConstant
Script.BASE_MSG = Base.TestFunc()

return ScriptC
