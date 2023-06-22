local Base = require("Base")

--- @class
--- @extends Base
local Script = {}
local ScriptC = gdclass(Script)

--- @classType Script
export type Script = Base.Base & typeof(Script) & {
    --- @property
    --- @default 4.25
    testProperty: number,

    --@1
}

return ScriptC
