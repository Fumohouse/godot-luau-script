local TestClass = gdclass("TestClass")

TestClass:RegisterSignal("testSignal")
    :Args(
        { name = "arg1", type = Enum.VariantType.FLOAT }
    )

TestClass:RegisterConstant("TEST_CONSTANT", Vector2.new(1, 2))

local testClassIndex = {}

function testClassIndex:PrivateMethod()
    return "hi there"
end

function TestClass:NonRegisteredMethod()
    return "what's up"
end

function TestClass._Init(obj, tbl)
    setmetatable(tbl, { __index = testClassIndex })

    tbl._notifHits = 0
    tbl.testField = 1
    tbl._testProperty = 3.25
    tbl.customTestPropertyValue = 1.25
end

function TestClass:_Ready()
end

TestClass:RegisterMethod("_Ready")

function TestClass:_Notification(what)
    if what == 42 then
        self._notifHits += 1
    end
end

TestClass:RegisterMethod("_Notification")

function TestClass:_ToString()
    return "my awesome class"
end

TestClass:RegisterMethod("_ToString")

function TestClass:TestMethod(arg1, arg2)
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

function TestClass:TestMethod2(arg1, arg2)
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

function TestClass:GetTestProperty()
    return 2 * self._testProperty
end

function TestClass:SetTestProperty(val)
    self._testProperty = val
end

TestClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :SetGet("SetTestProperty", "GetTestProperty")
    :Default(5.5)

function TestClass:GetTestProperty2()
    return "hello"
end

TestClass:RegisterProperty("testProperty2", Enum.VariantType.STRING)
    :SetGet(nil, "GetTestProperty2")
    :Default("hey")

function TestClass:SetTestProperty3(val)
end

TestClass:RegisterProperty("testProperty3", Enum.VariantType.STRING)
    :SetGet("SetTestProperty3")
    :Default("hey")

TestClass:RegisterProperty("testProperty4", Enum.VariantType.STRING)
    :Default("hey")

function TestClass:_GetPropertyList()
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

return TestClass
