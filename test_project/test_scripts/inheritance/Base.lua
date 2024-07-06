--- @class Base
--- @extends Object
local Base = {}
local BaseC = gdclass(Base)

export type Base = Object & typeof(Base) & {
    --- @property
    --- @default "hey"
    property1: string,

    --- @property
    --- @default "hi"
    property2: string,
}

--- @registerMethod
function Base:Method1(): string
    return "there"
end

--- @registerMethod
function Base:Method2(): string
    return "world"
end

return BaseC
