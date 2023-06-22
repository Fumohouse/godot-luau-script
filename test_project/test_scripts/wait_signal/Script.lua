--- @class
local Script = {}
local ScriptC = gdclass(Script)

--- @classType Script
export type Script = RefCounted & typeof(Script) & {
    --- @signal
    testSignal: SignalWithArgs<(number, string) -> ()>,
}

return ScriptC
