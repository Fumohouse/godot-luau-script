local TestClass = {
	extends = "TestBaseScript.lua",
	methods = {},
}

function TestClass:_Ready()
	print("TestScript: Ready!")

	self:TestMethod()
end

TestClass.methods["_Ready"] = {}

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
