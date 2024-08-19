-----------------
-- GODOT TYPES --
-----------------

export type SignalWithArgs<T> = Signal
export type TypedArray<T> = Array -- TODO: better way?
export type integer = number

declare class StringNameN end
declare class NodePathN end
export type StringName = string
export type NodePath = string
export type NodePathConstrained<T...> = NodePath

export type NodePathLike = string | NodePathN
export type StringNameLike = string | StringNameN
