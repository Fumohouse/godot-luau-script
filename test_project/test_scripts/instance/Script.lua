local TestClassImpl = {}
local TestClass = gdclass("TestClass")
    :RegisterImpl(TestClassImpl)

TestClass:RegisterSignal("testSignal")
    :Args(
        { name = "arg1", type = Enum.VariantType.FLOAT }
    )

TestClass:RegisterConstant("TEST_CONSTANT", Vector2.new(1, 2))

function TestClassImpl:NonRegisteredMethod()
    return "what's up"
end

function TestClassImpl:_Init()
    self.notifHits = 0
    self.testField = 1
    self._testProperty = 3.25
    self.customTestPropertyValue = 1.25
end

function TestClassImpl:_Ready()
end

TestClass:RegisterMethod("_Ready")

function TestClassImpl:_Notification(what)
    if what == 42 then
        self.notifHits += 1
    end
end

TestClass:RegisterMethod("_Notification")

function TestClassImpl:_ToString()
    return "my awesome class"
end

TestClass:RegisterMethod("_ToString")

function TestClassImpl:TestMethod(arg1, arg2)
    return string.format("%.1f, %s", arg1, arg2)
end

TestClass:RegisterMethod("TestMethod")
    :Args(
        {
            name = "arg1",
            type = Enum.VariantType.FLOAT
        },
        {
            name = "arg2",
            type = Enum.VariantType.STRING
        }
    )
    :DefaultArgs("hi")
    :ReturnVal({ type = Enum.VariantType.STRING })

function TestClassImpl:TestMethod2(arg1, arg2)
    return 3.14
end

TestClass:RegisterMethod("TestMethod2")
    :Args(
        {
            name = "arg1",
            type = Enum.VariantType.STRING
        },
        {
            name = "arg2",
            type = Enum.VariantType.INT
        }
    )
    :DefaultArgs("godot", 1)
    :ReturnVal({ type = Enum.VariantType.FLOAT })

function TestClassImpl:GetTestProperty()
    return 2 * self._testProperty
end

function TestClassImpl:SetTestProperty(val)
    self._testProperty = val
end

TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :SetGet("SetTestProperty", "GetTestProperty")
    :Default(5.5)

function TestClassImpl:GetTestProperty2()
    return "hello"
end

TestClass:RegisterProperty("testProperty2", Enum.VariantType.STRING)
    :SetGet(nil, "GetTestProperty2")
    :Default("hey")

function TestClassImpl:SetTestProperty3(val)
end

TestClass:RegisterProperty("testProperty3", Enum.VariantType.STRING)
    :SetGet("SetTestProperty3")
    :Default("hey")

TestClass:RegisterProperty("testProperty4", Enum.VariantType.STRING)
    :Default("hey")

function TestClassImpl:_GetPropertyList()
    return {
        {
            name = "custom/testProperty",
            type = Enum.VariantType.FLOAT
        }
    }
end

function TestClassImpl:_PropertyCanRevert(property)
    if property == "custom/testProperty" then
        return true
    end

    return false
end

function TestClassImpl:_PropertyGetRevert(property)
    if property == "custom/testProperty" then
        return 1.25
    end

    return nil
end

function TestClassImpl:_Set(property, value)
    if property == "custom/testProperty" then
        self.customTestPropertyValue = value
        return true
    end

    return false
end

function TestClassImpl:_Get(property)
    if property == "custom/testProperty" then
        return self.customTestPropertyValue
    end

    return nil
end

return TestClass
