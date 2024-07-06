--- @class
--- @extends Node
local Base = {}
local BaseC = gdclass(Base)

export type Base = Node & typeof(Base)

--@1

return BaseC
