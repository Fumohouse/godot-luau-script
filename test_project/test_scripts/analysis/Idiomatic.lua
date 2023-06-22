local Base = require("Base")

--- @class TestClass
--- @extends Base
--- @tool
--- @permissions INTERNAL OS
local TestClass = {}
local TestClassC = gdclass(TestClass)

--- @classType TestClass
export type TestClass = Base.Base & typeof(TestClass) & {
    --- @signal
    testSignal1: Signal,
    --- @signal
    testSignal2: SignalWithArgs<(argName1: number, Vector2, ...any) -> ()>,
} & {
    --- @propertyGroup TestGroup
    --- @property
    --- @default NodePath("Node3D/")
    --- @set setTestProperty1
    --- @get getTestProperty1
    testProperty1: NodePathConstrained<Camera3D, Camera2D>,

    --- @property
    --- @range 0.0 100.0 3.0
    testProperty2: number,

    field: number,
}

--- @registerConstant
TestClass.TEST_CONSTANT = Vector3.new(1, 2, 3)

--- @registerMethod ast
--- @param p1 Comment 1
--- @param p2 Comment 2
--- @param p3 Comment 3
--- @param p4 Comment 4
--- @param p5 Comment 5
--- @rpc authority reliable callLocal 3
function TestClass:TestMethod(p1: boolean, p2: Node3D, p3: Variant, p4: Base.Base, p5: TypedArray<Base.Base>, ...: Variant): number?
    return 3.14
end

return TestClassC
