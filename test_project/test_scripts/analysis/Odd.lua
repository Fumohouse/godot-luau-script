local TestClassImpl = {}
local TestClass = gdclass()
    :RegisterImpl(TestClassImpl)

function TestClassImpl:TestMethod()
end

do
    do
        do
            -- why would you ever want to do this
            return TestClass
        end
    end
end