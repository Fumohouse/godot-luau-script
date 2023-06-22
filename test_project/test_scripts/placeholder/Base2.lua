--- @class
--- @extends Node
local Base2 = {}
local Base2C = gdclass(Base2)

--- @classType Base2
export type Base2 = Node & typeof(Base2) & {
    --- @property
    --- @default Vector2(1, 2)
    baseProperty2: Vector2,
}

return Base2C
