--!strict
local MyClassImpl = {}
local MyClass = gdclass("MyClass", Resource)
    :RegisterImpl(MyClassImpl)

-- These items registered on the impl will be accessible by indexing `MyClass`,
-- making them accessible to other scripts if they `require` this one.
MyClassImpl.TestEnum = {
    ONE = 1,
    TWO = 2,
    THREE = 3,
}

MyClassImpl.TEST_CONSTANT = "hello!"

export type MyClass = Resource & typeof(MyClassImpl) & {
    -- Includes any custom fields/properties (essentially anything except methods)
    testProperty: number,
}

-- Register a property to be accessible in the editor
MyClass:RegisterProperty("testProperty", Enum.VariantType.FLOAT)
    :Range(0, 1, 0.1)
    :Default(0.5)

function MyClassImpl.TestMethodAST(self: MyClass, arg1: number, arg2: TypedArray<Resource>): Variant
    return 123
end

-- Register a method to be accessible to Godot, based on its type annotations
MyClass:RegisterMethodAST("TestMethodAST")
    :DefaultArgs(5, Array.new())

function MyClassImpl.TestMethodManual(self: MyClass, arg1: number, arg2: string): RefCounted
    return self
end

-- Register a method manually. Not recommended unless the above method fails or you are not using type checking.
MyClass:RegisterMethod("TestMethodManual")
    :Args(
        { name = "arg1", type = Enum.VariantType.FLOAT },
        { name = "arg2", type = Enum.VariantType.STRING }
    )
    :ReturnVal({ type = Enum.VariantType.OBJECT })

return MyClass
