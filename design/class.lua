--
-- example class
--

local Character = gdclass("Character", "RigidBody3D")

-- Tool
Character:Tool(false)

-- Initialization
local exampleIndex = {}

function exampleIndex:PrivateMethod()
    print("hi")
end

Character:Initialize(function(obj, tbl)
    obj.key = "value"
    setmetatable(tbl, { __index = exampleIndex })
end)

-- Notification
Character:Subscribe(13, function() -- 13 is READY. please use globals instead (numbers are used for typechecking).
    print("ready!")
end)

-- Method
Character:RegisterMethod("TestMethod", function(self: any, arg1: number, arg2: string): string
    return "hi!"
end, {
    -- ? this is essentially purely diagnostic information.
    -- it is not used anywhere (yet) (in-engine or in Luau script).
    args = {
        gdproperty({
            name = "arg1",
            type = 3 -- FLOAT
        }),
        gdproperty({
            name = "arg2",
            type = 4 -- STRING
        })
    },
    defaultArgs = { 1, "godot" },
    returnVal = gdproperty({ type = 4 })
})

return Character
