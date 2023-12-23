local scriptPath = "res://test_scripts/instance/Script.lua"

LuauInterface.SandboxService:CoreScriptAdd(scriptPath)
local script = load(scriptPath) :: Script?
assert(script)

local obj = RefCounted.new()
obj:SetScript(script)

do
    -- Hack to remove __namecall metatable
    local arr = Array.new()
    arr:PushBack(obj)
    obj = arr:Get(0)
end

do
    -- Notification
    obj:Notification(42)
    assert(obj.notifHits == 1)
end

do
    -- tostring
    assert(tostring(obj) == "my awesome class")
end

do
    -- Metatable

    -- Namecall
    assert(obj:NonRegisteredMethod() == "what's up")
    assert(obj:TestMethod(2.5, "asdf") == "2.5, asdf")

    -- Setget
    asserterror(function()
        obj.testSignal = 1234
    end, "cannot assign to signal 'testSignal'")

    assert(obj.testSignal == Signal.new(obj, "testSignal"))

    asserterror(function()
        obj.TEST_CONSTANT = 1234
    end, "cannot assign to constant 'TEST_CONSTANT'")

    assert(obj.TEST_CONSTANT == Vector2.new(1, 2))

    obj.testProperty = 2.5
    assert(obj.testProperty == 5)

    obj.testField = 2
    assert(obj.testField == 2)

    asserterror(function()
        obj.testProperty2 = "asdf"
    end, "property 'testProperty2' is read-only")

    asserterror(function()
        return obj.testProperty3
    end, "property 'testProperty3' is write-only")
end

do
    -- Callable
    local cb1 = Callable.new(obj, "TestMethod")
    assert(cb1:GetObject() == obj)
    assert(cb1:GetMethod() == "TestMethod")

    local cb2 = Callable.new(obj, "Call")
    assert(cb2:GetObject() == obj)
    -- Indicates the permissions were checked
    assert(cb2:GetMethod() == "call")
end
