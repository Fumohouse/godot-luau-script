local TestBaseScript = require("TestBaseScript")
local TestModule = require("TestModule.mod")

local TestClass = gdclass(nil, "TestBaseScript.lua")
	:Permissions(Enum.Permissions.INTERNAL)

function TestClass:Counter()
	for i = 3, 1, -1 do
		print(i.."!")
		wait(1)
	end
end

function TestClass:_Ready()
	print("TestScript: Ready!")

	self:TestMethod()
	TestBaseScript.TestMethod(self)

	TestAutoLoad:TestMethod()

	print(TestModule.testConstant)

	TestClass.Counter(self)

	-- Exit the test once complete
	print_rich("[color=green]Tests finished![/color] Exiting...")
	self:GetTree():Quit()
end

TestClass:RegisterMethod("_Ready")

TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
	:Default(1.5)

--
-- TEST HELPERS
--

-- Group
TestClass:PropertyGroup("Helper Test Properties")

-- Subgroup
TestClass:PropertySubgroup("Range")

-- Range
TestClass:RegisterProperty("testRange", Enum.VariantType.INT)
	:Range(0, 10, 1)

TestClass:RegisterProperty("testRangeF", Enum.VariantType.FLOAT)
	:Range(0, 10, 0.5)

-- Enums
TestClass:PropertySubgroup("Enums")
TestClass:RegisterProperty("testEnumInt", Enum.VariantType.INT)
	:Enum("One", "Two", "Three")
	:Default(0)

TestClass:RegisterProperty("testEnumString", Enum.VariantType.STRING)
	:Enum("One", "Two", "Three")
	:Default("Two")

TestClass:RegisterProperty("testSuggestion", Enum.VariantType.STRING)
	:Suggestion("One", "Two", "Three")
	:Default("")

TestClass:RegisterProperty("testFlags", Enum.VariantType.INT)
	:Flags("One", "Two", "Three")
	:Default(2)

-- File & Dir
TestClass:PropertySubgroup("File")
TestClass:RegisterProperty("testFile", Enum.VariantType.STRING)
	:File(false, "*.png,*.pdf")
	:Default("")

TestClass:RegisterProperty("testFileG", Enum.VariantType.STRING)
	:File(true, "*.png,*.svg")
	:Default("")

TestClass:RegisterProperty("testDir", Enum.VariantType.STRING)
	:Dir()
	:Default("")

TestClass:RegisterProperty("testDirG", Enum.VariantType.STRING)
	:Dir(true)
	:Default("")

-- String
TestClass:PropertySubgroup("String")
TestClass:RegisterProperty("testMultiline", Enum.VariantType.STRING)
	:Multiline()
	:Default("")

TestClass:RegisterProperty("testPlaceholder", Enum.VariantType.STRING)
	:TextPlaceholder("Placeholder!")
	:Default("")

-- Layers
TestClass:PropertySubgroup("Layers")
TestClass:RegisterProperty("testFlags2DRender", Enum.VariantType.INT)
	:Flags2DRenderLayers()

TestClass:RegisterProperty("testFlags2DPhysics", Enum.VariantType.INT)
	:Flags2DPhysicsLayers()

TestClass:RegisterProperty("testFlags2DNavigation", Enum.VariantType.INT)
	:Flags2DNavigationLayers()

TestClass:RegisterProperty("testFlags3DRender", Enum.VariantType.INT)
	:Flags3DRenderLayers()

TestClass:RegisterProperty("testFlags3DPhysics", Enum.VariantType.INT)
	:Flags3DPhysicsLayers()

TestClass:RegisterProperty("testFlags3DNavigation", Enum.VariantType.INT)
	:Flags3DNavigationLayers()

-- Easing
TestClass:PropertySubgroup("Other")
TestClass:RegisterProperty("testExpEasing", Enum.VariantType.FLOAT)
	:ExpEasing()
	:Default(0.5)

-- Color
TestClass:RegisterProperty("testColorNoAlpha", Enum.VariantType.COLOR)
	:NoAlpha()

-- Array
TestClass:RegisterProperty("testTypedArray", Enum.VariantType.ARRAY)
	:TypedArray("RID")

-- Resource
TestClass:RegisterProperty("testResource", Enum.VariantType.OBJECT)
	:Resource("Texture2D")

-- NodePath
TestClass:RegisterProperty("testNodePath", Enum.VariantType.NODE_PATH)
	:NodePath("Node3D")

TestClass:PropertyGroup("")

-- Category
TestClass:PropertyCategory("Test Category")
TestClass:RegisterProperty("inCategory", Enum.VariantType.STRING)
	:Default("hey!")

return TestClass
