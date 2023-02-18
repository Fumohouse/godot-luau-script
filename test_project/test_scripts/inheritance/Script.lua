local Base = require("Base")

local ScriptImpl = {}
local Script = gdclass("Script", Base)
    :RegisterImpl(ScriptImpl)

function ScriptImpl:GetProperty2()
    return "hihi"
end

Script:RegisterProperty("property2", Enum.VariantType.STRING)
    :SetGet(nil, "GetProperty2")
    :Default("hi")

function ScriptImpl:Method2()
    return "guy"
end

return Script
