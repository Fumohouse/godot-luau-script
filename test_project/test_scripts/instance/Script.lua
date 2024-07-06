--- @class TestClass
local TestClass = {}
local TestClassC = gdclass(TestClass)

export type TestClass = RefCounted & typeof(TestClass) & {
    --- @signal
    testSignal: SignalWithArgs<(arg1: number) -> ()>,

    --- @property
    --- @set SetTestProperty
    --- @get GetTestProperty
    --- @default 5.5
    testProperty: number,

    --- @property
    --- @get GetTestProperty2
    --- @default "hey"
    testProperty2: string,

    --- @property
    --- @set SetTestProperty3
    --- @default "hey"
    testProperty3: string,

    --- @property
    --- @default "hey"
    testProperty4: string,
}

--- @registerConstant
TestClass.TEST_CONSTANT = Vector2.new(1, 2)

function TestClass:NonRegisteredMethod()
    return "what's up"
end

function TestClass:_Init()
    self.notifHits = 0
    self.testField = 1
    self._testProperty = 3.25
    self.customTestPropertyValue = 1.25
end

--- @registerMethod
function TestClass:_Ready()
end

function TestClass:_Notification(what)
    if what == 42 then
        self.notifHits += 1
    end
end

function TestClass:_ToString()
    return "my awesome class"
end

--- @registerMethod
--- @defaultArgs ["hi"]
function TestClass:TestMethod(arg1: number, arg2: string): string
    return string.format("%.1f, %s", arg1, arg2)
end

--- @registerMethod
--- @defaultArgs ["godot", 1]
function TestClass:TestMethod2(arg1: string, arg2: integer): number
    return 3.14
end

--- @registerMethod
function TestClass:GetTestProperty()
    return 2 * self._testProperty
end

--- @registerMethod
function TestClass:SetTestProperty(val: number)
    self._testProperty = val
end

--- @registerMethod
function TestClass:GetTestProperty2()
    return "hello"
end

--- @registerMethod
function TestClass:SetTestProperty3(val: string)
end

function TestClass:_GetPropertyList(): {GDProperty}
    return {
        {
            name = "custom/testProperty",
            type = Enum.VariantType.FLOAT
        }
    }
end

function TestClass:_PropertyCanRevert(property)
    if property == "custom/testProperty" then
        return true
    end

    return false
end

function TestClass:_PropertyGetRevert(property)
    if property == "custom/testProperty" then
        return 1.25
    end

    return nil
end

function TestClass:_Set(property, value)
    if property == "custom/testProperty" then
        self.customTestPropertyValue = value
        return true
    end

    return false
end

function TestClass:_Get(property)
    if property == "custom/testProperty" then
        return self.customTestPropertyValue
    end

    return nil
end

return TestClassC
