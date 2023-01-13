local TestAutoLoad = gdclass(nil, "Node")

function TestAutoLoad:_Ready()
	print("TestAutoLoad: Ready!")
end

TestAutoLoad:RegisterMethod("_Ready")

function TestAutoLoad:TestMethod()
	print("TestAutoLoad: TestMethod")
end

TestAutoLoad:RegisterMethod("TestMethod")

return TestAutoLoad
