local TestBaseScript = require("TestBaseScript")
local TestModule = require("TestModule.mod")

local TestClassImpl = {}
local TestClass = gdclass(nil, TestBaseScript)
    :Tool(true) -- For custom properties in editor
    :Permissions(Enum.Permissions.INTERNAL)
    :RegisterImpl(TestClassImpl)

function TestClassImpl:Counter()
    for i = 3, 1, -1 do
        print(i.."!")
        wait(1)
    end
end

function TestClassImpl._Init(obj, tbl)
    tbl.customProperty = "hey"
end

function TestClassImpl:_Ready()
    print("TestScript: Ready!")

    self:TestMethod()
    TestBaseScript.TestMethod(self)

    gdglobal("TestAutoLoad"):TestMethod()

    print(TestModule.testConstant)

    if not Engine.singleton:IsEditorHint() then
        self:Counter()

        -- Exit the test once complete
        print_rich("[color=green]Tests finished![/color] Exiting...")
        self:GetTree():Quit()
    end
end

TestClass:RegisterMethod("_Ready")

TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :Default(1.5)

function TestClassImpl:_GetPropertyList()
    return {
        { name = "Custom Property", usage = Enum.PropertyUsageFlags.GROUP },
        { name = "customProperty", type = Enum.VariantType.STRING },
        { name = "", usage = Enum.PropertyUsageFlags.GROUP },
    }
end

function TestClassImpl:_PropertyCanRevert(property)
    if property == "customProperty" then
        return true
    end

    return false
end

function TestClassImpl:_PropertyGetRevert(property)
    if property == "customProperty" then
        return "hey"
    end

    return nil
end

function TestClassImpl:_Set(property, value)
    if property == "customProperty" then
        self.customProperty = value
        return true
    end

    return false
end

function TestClassImpl:_Get(property)
    if property == "customProperty" then
        return self.customProperty
    end

    return nil
end

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

TestClass:RegisterProperty("testEnumString", Enum.VariantType.STRING)
    :Enum("One", "Two", "Three")
    :Default("Two")

TestClass:RegisterProperty("testSuggestion", Enum.VariantType.STRING)
    :Suggestion("One", "Two", "Three")

TestClass:RegisterProperty("testFlags", Enum.VariantType.INT)
    :Flags("One", "Two", "Three")
    :Default(2)

-- File & Dir
TestClass:PropertySubgroup("File")
TestClass:RegisterProperty("testFile", Enum.VariantType.STRING)
    :File(false, "*.png,*.pdf")

TestClass:RegisterProperty("testFileG", Enum.VariantType.STRING)
    :File(true, "*.png,*.svg")

TestClass:RegisterProperty("testDir", Enum.VariantType.STRING)
    :Dir()

TestClass:RegisterProperty("testDirG", Enum.VariantType.STRING)
    :Dir(true)

-- String
TestClass:PropertySubgroup("String")
TestClass:RegisterProperty("testMultiline", Enum.VariantType.STRING)
    :Multiline()

TestClass:RegisterProperty("testPlaceholder", Enum.VariantType.STRING)
    :TextPlaceholder("Placeholder!")

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
    :TypedArray(RID)

-- Resource
TestClass:RegisterProperty("testResource", Enum.VariantType.OBJECT)
    :Resource(Texture2D)

-- NodePath
TestClass:RegisterProperty("testNodePath", Enum.VariantType.NODE_PATH)
    :NodePath(Node3D)

TestClass:PropertyGroup("")

-- Category
TestClass:PropertyCategory("Test Category")
TestClass:RegisterProperty("inCategory", Enum.VariantType.STRING)
    :Default("hey!")

return TestClass
