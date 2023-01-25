local TestClassImpl = {}
local TestClass = gdclass()
    :RegisterImpl(TestClassImpl)

type TypeAlias = {
    field: number,
}

function TestClassImpl:TestMethod()
end

return TestClass
