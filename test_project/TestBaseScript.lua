local TestBaseScript = {
	extends = "Node3D",
	methods = {},
	properties = {},
}

function TestBaseScript:TestMethod()
	print("TestBaseScript: TestMethod")
end

TestBaseScript.methods["TestMethod"] = {}

TestBaseScript.properties["baseProperty"] = {
	property = gdproperty({ name = "baseProperty", type = Enum.VariantType.STRING }),
	usage = Enum.PropertyUsageFlags.STORAGE,
	default = "hi"
}

return TestBaseScript
