local TestClass = gdclass(nil, "TestBaseScript.lua")
	:Permissions(Enum.Permissions.INTERNAL)

function TestClass:_Ready()
	print("TestScript: Ready!")

	self:TestMethod()

	-- Exit the test once complete
	print_rich("[color=green]Tests finished![/color] Exiting...")
	self:GetTree():Quit()
end

TestClass:RegisterMethod("_Ready")

TestClass:RegisterProperty("testProperty", { type = Enum.VariantType.FLOAT })
	:Default(1.5)

return TestClass
