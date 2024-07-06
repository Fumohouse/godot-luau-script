--- @class TestClass
--- @tool
local TestClass = {}
local TestClassC = gdclass(TestClass)

export type TestClass = RefCounted & typeof(TestClass) & {
    --- @signal
    testSignal: SignalWithArgs<(number) -> ()>,

    --- @property
    --- @default 5.5
    testProperty: number,
}

--- @registerMethod
--- @rpc anyPeer reliable callLocal 4
function TestClass:TestRpc()
end

--- @registerMethod
function TestClass:TestMethod()
end

--- @registerMethod
function TestClass:__WeirdMethodName()
end

return TestClassC
