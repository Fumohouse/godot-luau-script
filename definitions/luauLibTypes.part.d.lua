---------------------
-- LUAGD_LIB TYPES --
---------------------

--[[--
    Extension to Luau's default string library which includes some commonly used methods
    which are not implemented in Luau.
]]
declare strext: {
    --[[--
        Finds whether a string starts with another string.
        @param self The string being queried.
        @param str The start string.
    ]]
    startswith: (self: string, str: string) -> boolean,

    --[[--
        Finds whether a string ends with another string.
        @param self The string being queried.
        @param str The end string.
    ]]
    endswith: (self: string, str: string) -> boolean,
}

--[[--
    Constructs a new `StringName`.
    @param str The string to use.
]]
declare function SN(str: string): StringName

--[[--
    Constructs a new `NodePath`.
    @param str The string to use.
]]
declare function NP(str: string): NodePath

-- TODO: constrain to Resource type?
--[[--
    Loads a resource. Mostly an alias for `ResourceLoader.singleton:Load()`.
    @param path The **relative** path to the resource from this script.
]]
declare function load<T>(path: string): T?

--[[--
    Determines the Godot Variant type of a value, or `nil` if the value is not Variant-compatible.
    @param value The value.
]]
declare function gdtypeof(value: any): EnumVariantType?

--------------------
-- LUAU_LIB TYPES --
--------------------

export type EnumPermissions = number

declare class EnumPermissions_INTERNAL
    --- Used for functionality that is available to all scripts by default.
    BASE: EnumPermissions

    --- Used for all functionality which is not part of any special permission level or `BASE`.
    INTERNAL: EnumPermissions

    --- Used for the `OS` singleton.
    OS: EnumPermissions

    --- Used for any functionality with read or write access to the filesystem.
    FILE: EnumPermissions

    --- Used for any functionality allowing HTTP listen/requests.
    HTTP: EnumPermissions
end

--[[--
    Indicates permission levels for script execution.
    Only core scripts can declare permissions, through @see GDClassDefinition.Permissions.
]]
declare EnumPermissions: EnumPermissions_INTERNAL

--[[--
    A table type used to declare properties, method arguments, and return values.
]]
export type GDProperty = {
    type: EnumVariantType?,
    name: string?,
    hint: EnumPropertyHint?,
    hintString: string?,
    usage: EnumPropertyUsageFlags?,
    className: string?,
}

--[[--
    A type used for registering methods to a @see GDClassDefinition.
]]
declare class GDMethod
    --[[--
        Registers arguments for the method.
        @varargs Arguments to be registered.
    ]]
    function Args(self, ...: GDProperty): GDMethod

    --[[--
        Registers default arguments for the method.
        @varargs Default values for arguments, with the rightmost value corresponding to the rightmost argument.
    ]]
    function DefaultArgs(self, ...: Variant): GDMethod

    --[[--
        Registers a return value for the method.
        @param val The return type.
    ]]
    function ReturnVal(self, val: GDProperty): GDMethod

    --[[--
        Registers any special flags for the method.
        @param flags The flags.
    ]]
    function Flags(self, flags: EnumMethodFlags): GDMethod
end

--[[--
    A type used for registering properties to a @see GDClassDefinition.
]]
declare class GDClassProperty
    --[[--
        Registers a default value for the property.
        @param value The value.
    ]]
    function Default(self, value: Variant): GDClassProperty

    --[[--
        Registers a setter and/or a getter for the property.
        @param setter The method name of the setter.
        @param getter The method name of the getter.
    ]]
    function SetGet(self, setter: string?, getter: string?): GDClassProperty

    -- HINT HELPERS --

    --[[--
        Registers a range and step for a numeric property.
        @param min The minimum value.
        @param max The maximum value.
        @param step The increment Godot will use in the editor.
    ]]
    function Range(self, min: number, max: number, step: number?): GDClassProperty

    --[[--
        Registers enum values to be used by the editor, for a string or integer property.
        @varargs The enum values.
    ]]
    function Enum(self, ...: string): GDClassProperty

    --[[--
        Registers a set of suggestions for a string property, similar to @see GDClassProperty.Enum
        but allowing any value to be input in the editor.
        @varargs The suggested values.
    ]]
    function Suggestion(self, ...: string): GDClassProperty

    --[[--
        Registers a set of bit flags which can be set/unset for an integer property.
        @varargs The names of each flag.
    ]]
    function Flags(self, ...: string): GDClassProperty

    --[[--
        Indicates that a string property should be for a file path.
        @param isGlobal Whether the path is global or local to the `res://` filesystem.
        @varargs An accepted set of path filters, e.g. `*.png`.
    ]]
    function File(self, isGlobal: boolean, ...: string): GDClassProperty

    --[[--
        Indicates that a string property should be for a directory path.
        @param isGlobal Whether the path is global or local to the `res://` filesystem.
    ]]
    function Dir(self, isGlobal: boolean): GDClassProperty

    --[[--
        Indicates that a string property should have a multiline text box in the editor.
    ]]
    function Multiline(self): GDClassProperty

    --[[--
        Registers a placeholder to be shown in the text box for a string property with no value.
        @param placeholder The placeholder.
    ]]
    function TextPlaceholder(self, placeholder: string): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 2D render layers.
    ]]
    function Flags2DRenderLayers(self): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 2D physics layers.
    ]]
    function Flags2DPhysicsLayers(self): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 2D navigation layers.
    ]]
    function Flags2DNavigationLayers(self): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 3D render layers.
    ]]
    function Flags3DRenderLayers(self): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 3D physics layers.
    ]]
    function Flags3DPhysicsLayers(self): GDClassProperty

    --[[--
        Indicates an integer property should show the flags for 3D navigation layers.
    ]]
    function Flags3DNavigationLayers(self): GDClassProperty

    --[[--
        Indicates a float property should be edited through an exponential easing function.
        @param hint Either "attenuation" to indicate a flipped curve or "positive_only" to limit values to positive only.
    ]]
    function ExpEasing(self, hint: "attenuation" | "positive_only"): GDClassProperty

    --[[--
        Indicates a color property should not allow alpha value to be edited.
    ]]
    function NoAlpha(self): GDClassProperty

    --[[--
        Indicates an array property should be constrained to a specific type.
        @param type The name of the type.
        @param isResource Whether the type is the class name of a resource.
    ]]
    function TypedArray(self, type: GDClassDefinition | ClassGlobal): GDClassProperty

    --[[--
        Indicates an object property should be constrained to a specific resource type.
        @param type The name of the type.
    ]]
    function Resource(self, type: GDClassDefinition | ClassGlobal): GDClassProperty

    --[[--
        Constrains a NodePath property to specific node types.
        @varargs The names of the types.
    ]]
    function NodePath(self, ...: GDClassDefinition | ClassGlobal): GDClassProperty
end

declare class GDSignal
    --[[--
        Registers arguments for the signal.
        @varargs Arguments to be registered.
    ]]
    function Args(self, ...: GDProperty): GDMethod
end

--[[--
    A type used for registering an RPC to a @see GDClassDefinition.
]]
export type GDRpcConfig = {
    rpcMode: ClassEnumMultiplayerAPI_RPCMode,
    transferMode: ClassEnumMultiplayerPeer_TransferMode,
    callLocal: boolean,
    channel: number,
}

--[[--
    A type returned from a script to register a custom class type.
]]
declare class GDClassDefinition
    --[[--
        Constructs a new instance of the base object with this script.
    ]]
    new: () -> Object

    --[[--
        Indicates whether this script is instantiable in the editor (tool script).
        @param isTool Whether this script is a tool script.
    ]]
    function Tool(self, isTool: boolean): GDClassDefinition

    --[[--
        Declares permissions for instances of this script. Only valid for core scripts.
        @param permissions Declared permissions.
    ]]
    function Permissions(self, permissions: EnumPermissions): GDClassDefinition

    --[[--
        Declares an icon path for this type.
        @param path Path to a .svg image.
    ]]
    function IconPath(self, path: string): GDClassDefinition

    --[[--
        Registers an implementation table for any methods, constants, etc.
        Items in this table will be accessible by indexing this class definition.
        This is used primarily to assist the typechecker.
        @param table The implementation table.
    ]]
    RegisterImpl: <T>(self: GDClassDefinition, table: T) -> GDClassDefinition & T

    -- REGISTRATION METHODS --

    --[[--
        Registers a method.
        @param name The name of the method defined in the implementation table.
    ]]
    function RegisterMethod(self, name: string): GDMethod

    --[[--
        Registers a method and automatically declares its arguments and return value based on type annotations.
        @param name The name of the method defined in the implementation table.
    ]]
    function RegisterMethodAST(self, name: string): GDMethod

    --[[--
        Registers a property which will be accessible by indexing instances of this script.
        @param name The name of the property.
        @param propertyOrType The @see GDProperty indicating the type of this property, or an @see EnumVariantType if advanced information is not needed.
    ]]
    function RegisterProperty(self, name: string, propertyOrType: EnumVariantType | GDProperty): GDClassProperty

    --[[--
        Registers a signal which will be accessible by indexing instances of this script.
        @param name The name of the signal.
    ]]
    function RegisterSignal(self, name: string): GDSignal

    --[[--
        Registers an RPC.
        @param rpcConfig The RPC configuration.
    ]]
    function RegisterRpc(self, rpcConfig: GDRpcConfig)

    --[[--
        Registers a constant which will be accessible by indexing instances of this script.
        The recommended way create constants unless they must be accessible to Godot is by defining them on the implementation table.
        @param name The name of the constant.
        @param value The value of the constant.
    ]]
    function RegisterConstant(self, name: string, value: Variant)

    -- METAMETHODS --

    --[[--
        Gets a value on the implementation table, erroring if one has not been created.
    ]]
    function __index(self, key: string): any
end

--[[--
    Creates a new @see GDClassDefinition.
    @param name The name of the class. This makes it "global" in the Godot editor, meaning it will, for example, show up in the Node/Resource creation dialogs.
    @param extends The Luau or Godot class this class extends.
]]
declare function gdclass(name: string?, extends: GDClassDefinition | ClassGlobal?): GDClassDefinition

--[[--
    Yields the current thread and waits before resuming.
    @param duration The duration to wait. If the engine is affected by a time factor, this duration will be affected by it.
]]
declare function wait(duration: number): number

--[[--
    Yields the current thread and waits for the signal to be emitted before resuming.
    @param signal The signal.
    @param timeout The number of seconds to wait before timing out (default: 10 seconds).
    @return The first return value is whether the signal was emitted (true) or timed out (false), and subsequent values are the arguments passed to the signal when it was emitted.
]]
declare function wait_signal<T...>(signal: Signal, timeout: number?): (true, T...) | false

--[[--
    Gets a global constant (e.g. AutoLoad) which was defined in the Godot editor.
    @param name The name of the global constant.
]]
declare function gdglobal(name: string | StringName): Variant
