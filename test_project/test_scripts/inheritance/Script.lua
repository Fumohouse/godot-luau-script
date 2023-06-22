local Base = require("Base")

--- @class Script
--- @extends Base
local Script = {}
local ScriptC = gdclass(Script)

--- @classType Script
export type Script = Base.Base & typeof(Script) & {
    --- @property
    --- @get GetProperty2
    property2: string,
}

--- @registerMethod
function Script:GetProperty2(): string
    return "hihi"
end

function Script:Method2()
    return "guy"
end

return ScriptC
