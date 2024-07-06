local Base = require("Base")

--- @class
--- @extends Base
local Script = {}
local ScriptC = gdclass(Script)

export type Script = Base.Base & typeof(Script)

--@1

return ScriptC
