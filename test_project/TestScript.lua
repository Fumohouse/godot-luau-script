local TestClass = gdclass({
	extends = "TestBaseScript.lua",
	permissions = Enum.Permissions.INTERNAL
})

function TestClass:_Ready()
	print("TestScript: Ready!")

	self:TestMethod()

	-- Exit the test once complete
	print_rich("[color=green]Tests finished![/color] Exiting...")
	self:GetTree():Quit()
end

TestClass:RegisterMethod("_Ready", {})

TestClass:RegisterProperty("testProperty", {
	property = gdproperty({ type = Enum.VariantType.FLOAT }),
	default = 1.5
})

--[[
function TestClass:_Process(delta)
	print("Processing ", delta)
end

TestClass:RegisterMethod("_Process", {
	args = {
		gdproperty({
			name = "delta",
			type = Enum.VariantType.FLOAT
		})
	}
})
]]

return TestClass
