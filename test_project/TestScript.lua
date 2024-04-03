local TestBaseScript = require("TestBaseScript")
local TestModule = require("TestModule.mod")

--- @class
--- @extends TestBaseScript
--- @tool
--- @permissions INTERNAL
local TestClass = {}
local TestClassC = gdclass(TestClass)

--- @classType TestClass
export type TestClass = TestBaseScript.TestBaseScript & typeof(TestClass) & {
    --- @property
    --- @default 1.5
    testProperty: number,

    --- @propertyGroup Helper Test Properties
    --- @propertySubgroup Range
    --- @property
    --- @range 0 10 1
    testRange: integer,

    --- @property
    --- @range 0 10 0.5
    testRangeF: number,

    --- @propertySubgroup Enums
    --- @property
    --- @enum One Two Three
    testEnumInt: integer,

    --- @property
    --- @enum One Two Three
    --- @default "Two"
    testEnumString: string,

    --- @property
    --- @suggestion One Two Three
    testSuggestion: string,

    --- @property
    --- @flags One Two Three
    --- @default 2
    testFlags: integer,

    --- @propertySubgroup File
    --- @property
    --- @file *.png *.pdf
    testFile: string,

    --- @property
    --- @file global *.png *.svg
    testFileG: string,

    --- @property
    --- @dir
    testDir: string,

    --- @property
    --- @dir global
    testDirG: string,

    --- @propertySubgroup String
    --- @property
    --- @multiline
    testMultiline: string,

    --- @property
    --- @placeholderText Placeholder!
    testPlaceholder: string,

    --- @propertySubgroup Layers
    --- @property
    --- @flags2DRenderLayers
    testFlags2DRender: integer,

    --- @property
    --- @flags2DPhysicsLayers
    testFlags2DPhysics: integer,

    --- @property
    --- @flags2DNavigationLayers
    testFlags2DNavigation: integer,

    --- @property
    --- @flags3DRenderLayers
    testFlags3DRender: integer,

    --- @property
    --- @flags3DPhysicsLayers
    testFlags3DPhysics: integer,

    --- @property
    --- @flags3DNavigationLayers
    testFlags3DNavigation: integer,

    --- @propertySubgroup Other
    --- @property
    --- @expEasing
    testExpEasing: number,

    --- @property
    --- @noAlpha
    testColorNoAlpha: Color,

    --- @property
    testTypedArray: TypedArray<Vector2>,

    --- @property
    testResource: Texture2D,

    --- @property
    --- @propertyGroup
    testNodePath: NodePathConstrained<Node3D>,

    --- @propertyCategory Test Category
    --- @property
    --- @default "hey!"
    inCategory: string,
}

function TestClass:_Init()
    self.customProperty = "hey"
end

--- @registerMethod
function TestClass:_Ready()
    print("TestScript: Ready!")

    self:TestMethod()
    TestBaseScript.TestMethod(self)

    gdglobal("TestAutoLoad"):TestMethod()

    print(TestModule.testConstant)

    LuauInterface.DebugService:Exec("print('hello from exec!')")
    print("EXEC ERR: ", LuauInterface.DebugService:Exec("error('ERROR!')")) -- in thread error
    LuauInterface.DebugService:Exec("wait(1); error('ERROR!')") -- task scheduler error

    if not Engine.singleton:IsEditorHint() then
        -- Exit the test once complete
        print_rich("[color=green]Tests finished![/color] Exiting...")
        wait(1)
        self:GetTree():Quit()
    end
end

function TestClass:_GetPropertyList()
    return {
        { name = "Custom Property", usage = Enum.PropertyUsageFlags.GROUP },
        { name = "customProperty", type = Enum.VariantType.STRING },
        { name = "", usage = Enum.PropertyUsageFlags.GROUP },
    }
end

function TestClass:_PropertyCanRevert(property)
    if property == "customProperty" then
        return true
    end

    return false
end

function TestClass:_PropertyGetRevert(property)
    if property == "customProperty" then
        return "hey"
    end

    return nil
end

function TestClass:_Set(property, value)
    if property == "customProperty" then
        self.customProperty = value
        return true
    end

    return false
end

function TestClass:_Get(property)
    if property == "customProperty" then
        return self.customProperty
    end

    return nil
end

return TestClassC
