local TestResource = gdclass("TestResource", Resource)

TestResource:RegisterProperty("testProperty", Enum.VariantType.STRING)
	:Multiline()

return TestResource
