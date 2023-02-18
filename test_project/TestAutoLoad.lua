local TestAutoLoadImpl = {}
local TestAutoLoad = gdclass(nil, Node)
	:Tool(true)
	:RegisterImpl(TestAutoLoadImpl)

function TestAutoLoadImpl:_Ready()
	print("TestAutoLoad: Ready!")
end

TestAutoLoad:RegisterMethod("_Ready")

function TestAutoLoadImpl:TestMethod()
	print("TestAutoLoad: TestMethod")
end

TestAutoLoad:RegisterMethod("TestMethod")

return TestAutoLoad
