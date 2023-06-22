--- @class
--- @extends Node
local Base = {}
local BaseC = gdclass(Base)

--- @classType Base
export type Base = Node & typeof(Base) & {
    --- @property
    --- @default "hello"
    baseProperty: string,
}

return BaseC
