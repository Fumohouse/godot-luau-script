local TestClass = {
	extends = "TestBaseScript.lua",
	permissions = Enum.Permissions.INTERNAL,
	methods = {},
	properties = {},
}

function TestClass:_Ready()
	print("TestScript: Ready!")

	self:TestMethod()

	-- Exit the test once complete
	print_rich("[color=green]Tests finished![/color] Exiting...")
	self:GetTree():Quit()
end

TestClass.methods["_Ready"] = {}

TestClass.properties["testProperty"] = {
	property = gdproperty({ name = "testProperty", type = Enum.VariantType.FLOAT }),
	usage = Enum.PropertyUsageFlags.STORAGE,
	default = 1.5
}

--[[
function TestClass:_Process(delta)
	print("Processing ", delta)
end

TestClass.methods["_Process"] = {
	args = {
		gdproperty({
			name = "delta",
			type = Enum.VariantType.FLOAT
		})
	}
}
]]

return TestClass
