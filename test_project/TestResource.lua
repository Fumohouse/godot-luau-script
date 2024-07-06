--- @class TestResource
--- @extends Resource
local TestResource = {}
local TestResourceC = gdclass(TestResource)

export type TestResource = Resource & typeof(TestResource) & {
	--- @property
	--- @multiline
	testProperty: string,
}

return TestResourceC
