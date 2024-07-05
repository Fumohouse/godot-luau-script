--!strict
--- @class MyClass
--- @extends Resource
local MyClass = {}

-- 'C' stands for class. The `gdclass` method gives access to the `.new`
-- function.
local MyClassC = gdclass(MyClass)

-- These items registered will be accessible by indexing `MyClass`,
-- making them accessible to other scripts if they `require` this one.
MyClass.TestEnum = {
    ONE = 1,
    TWO = 2,
    THREE = 3,
}

--- @registerConstant
MyClass.TEST_CONSTANT = "hello!"

--- @classType MyClass
export type MyClass = Resource & typeof(MyClassImpl) & {
    --- @property
    --- @range 0.0 1.0 0.1
    --- @default 0.5
    testProperty: number,
}

--- Register a method to be accessible to Godot, based on its type annotations
--- @registerMethod
--- @param arg1 Comment 1
--- @param arg2 Comment 2
--- @return Something
function MyClass.TestMethodAST(self: MyClass, arg1: number, arg2: TypedArray<Resource>): Variant
    return 123
end

return MyClassC
