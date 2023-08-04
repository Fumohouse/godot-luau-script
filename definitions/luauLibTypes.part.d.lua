---------------------
-- LUAGD_LIB TYPES --
---------------------

--- Constructs a new `StringName`.
--- @param str The string to use.
declare function SN(str: string): StringNameN

--- Constructs a new `NodePath`.
--- @param str The string to use.
declare function NP(str: string): NodePathN

-- TODO: constrain to Resource type?
--- Loads a resource. Mostly an alias for `ResourceLoader.singleton:Load()`.
--- @param path The **relative** path to the resource from this script.
declare function load<T>(path: string): T?

--- Determines the Godot Variant type of a value, or `nil` if the value is not Variant-compatible.
--- @param value The value.
declare function gdtypeof(value: any): EnumVariantType?

--------------------
-- LUAU_LIB TYPES --
--------------------

--- A table type used to declare properties, method arguments, and return values.
export type GDProperty = {
    type: EnumVariantType?,
    name: string?,
    hint: EnumPropertyHint?,
    hintString: string?,
    usage: EnumPropertyUsageFlags?,
    className: string?,
}

--- A type returned from a script to register a custom class type.
declare class GDClass
    --- Constructs a new instance of the base object with this script.
    new: () -> Object
end

--- Creates a new @see GDClass.
--- @param tbl The implementation table for any methods, constants, etc. Items in this table will be accessible by indexing this class definition.
declare function gdclass<T>(tbl: T): T & GDClass

--- Yields the current thread and waits before resuming.
--- @param duration The duration to wait. If the engine is affected by a time factor, this duration will be affected by it.
declare function wait(duration: number): number

--- Yields the current thread and waits for the signal to be emitted before resuming.
--- @param signal The signal.
--- @param timeout The number of seconds to wait before timing out (default: 10 seconds).
--- @return The first return value is whether the signal was emitted (true) or timed out (false), and subsequent values are the arguments passed to the signal when it was emitted.
declare function wait_signal<T...>(signal: Signal, timeout: number?): (true, T...) | false

--- Gets a global constant (e.g. AutoLoad) which was defined in the Godot editor.
--- @param name The name of the global constant.
declare function gdglobal(name: StringNameLike): Variant
