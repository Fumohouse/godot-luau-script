local TestBaseScript = {
    extends = "Node3D",
    methods = {},
}

function TestBaseScript:TestMethod()
	print("TestBaseScript: TestMethod")
end

TestBaseScript.methods["TestMethod"] = {}

return TestBaseScript
