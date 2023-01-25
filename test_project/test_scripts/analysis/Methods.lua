local TestClassImpl = {}
local TestClass = gdclass()
    :RegisterImpl(TestClassImpl)

function TestClassImpl:WithSelf(arg1: number): string
    return ""
end

TestClass:RegisterMethodAST("WithSelf")

function TestClassImpl.WithoutSelf(self: RefCounted, arg1: number): string
    return ""
end

TestClass:RegisterMethodAST("WithoutSelf")

function TestClassImpl:SpecialArg(testObj: Node3D, testRes: Texture2D, testArray: TypedArray<Texture2D>)
end

TestClass:RegisterMethodAST("SpecialArg")

function TestClassImpl:Vararg(testArg: number, ...: Variant)
end

TestClass:RegisterMethodAST("Vararg")

return TestClass
