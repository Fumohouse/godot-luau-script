--- @class
local TestBaseScript = {}
local TestBaseScriptC = gdclass(TestBaseScript)

--- @classType TestBaseScript
export type TestBaseScript = RefCounted & typeof(TestBaseScript) & {
	--- @property
	--- @default "hi"
	baseProperty: string,
}

--- @registerMethod
function TestBaseScript:TestMethod()
	print("TestBaseScript: TestMethod")
end

return TestBaseScriptC
