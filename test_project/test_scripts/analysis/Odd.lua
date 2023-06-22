--- @class
local TestClass = {}
local TestClassC = gdclass(TestClass)

do
    do
        do
            -- why would you ever want to do this
            return TestClassC
        end
    end
end
