local TestClass = {
	extends = "Node3D",
	methods = {},
}

function TestClass:_Ready()
	print("TestScript: Ready!")
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
			type = Enum.VariantType.TYPE_FLOAT
		})
	}
}
]]

return TestClass
