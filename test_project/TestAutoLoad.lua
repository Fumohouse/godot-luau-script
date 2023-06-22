--- @class
--- @extends Node
--- @tool
local TestAutoLoad = {}
local TestAutoLoadC = gdclass(TestAutoLoad)

--- @registerMethod
function TestAutoLoad:_Ready()
	print("TestAutoLoad: Ready!")
end

--- @registerMethod
function TestAutoLoad:TestMethod()
	print("TestAutoLoad: TestMethod")
end

return TestAutoLoadC
