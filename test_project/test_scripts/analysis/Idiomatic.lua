--[[
    block comment
]]

local TestClassImpl = {}
local TestClass = gdclass()
    :RegisterImpl(TestClassImpl)

type TypeAlias = {
    field: number,
}

-- comment
function TestClassImpl:TestMethod()
    return 12 -- comment 2
end

return TestClass
