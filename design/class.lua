--
-- example class
--

local Character = {
    name = "Character",
    extends = "RigidBody3D",
    tool = false,
    methods = {},
    properties = {}
}

-- Initialization
function Character._Init(obj, tbl)
    obj.key = "value"
    -- you can do anything here, like initialize values or set a metatable for `tbl`
end

-- Notification
function Character._Notification(self, what)
    if what == 13 then
        print("ready!")
    end
end

function Character._Ready(self)
    print("ready (easier)!")
end

-- Method
function Character.TestMethod(self: any, arg1: number, arg2: string): string
    return "hi!"
end

Character.methods["TestMethod"] = {
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
}

-- Property
Character.properties["testProperty"] = {
    property = gdproperty({
        name = "testProperty",
        type = 3
    }),
    getter = "GetTestProperty",
    setter = "SetTestProperty",
    default = 3.5
}

return Character
