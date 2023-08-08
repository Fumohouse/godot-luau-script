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
--- Loads a resource. If the current script is not a core script, accessible
--- paths are restricted by SandboxService.
--- @param path The relative/absolute path to the resource from this script.
declare function load<T>(path: string): T?

--- Saves a resource. If the current script is not a core script, accessible
--- paths are restricted by SandboxService.
--- @param path The absolute path to save the resource to.
declare function save(resource: Resource, path: string, flags: ClassEnumResourceSaver_SaverFlags?)

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

--------------
-- SERVICES --
--------------

--- Service handling script sandboxing
declare class SandboxService
    --- Returns whether a script is a core script.
    --- @param path The path to query.
    function IsCoreScript(self, path: string): boolean

    --- Initiates core script discovery from the project root. By default, any
    --- files present at this time will be considered core scripts.
    function DiscoverCoreScripts(self)

    --- Ignores a path from core script discovery. Any file paths starting with
    --- the given path will be ignored.
    --- @param path The path to ignore.
    function CoreScriptIgnore(self, path: string)

    --- Adds a script path as a core script.
    --- @param path The path to add.
    function CoreScriptAdd(self, path: string)

    --- Returns an array of all core scripts.
    function CoreScriptList(self): Array

    --- Adds access for non-core scripts to read or write resources to the given
    --- path and its subdirectories.
    --- @param path The path to add.
    function ResourceAddPathRW(self, path: string)

    --- Adds access for non-core scripts to read from the given path and its
    --- subdirectories.
    --- @param path The path to add.
    function ResourceAddPathRO(self, path: string)

    --- Removes access for non-core scripts to read or write from the given path
    --- and its subdirectories.
    --- @param path The path to remove.
    function ResourceRemovePath(self, path: string)
end

--- The main service through which all services are found.
declare LuauInterface: {
    SandboxService: SandboxService,
}
