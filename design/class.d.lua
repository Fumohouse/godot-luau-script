--
-- internal api layout
--

type GDPropertyParameters = {
    type: number, -- Variant::Type
    name: string?, -- default: ""
    hint: number?, -- PropertyHint, default: NONE
    hintString: string?, -- default: ""
    usage: number?, -- PropertyUsageFlags, default: NONE
    className: string? -- For clarifying OBJECT types. default: ""
}

type GDProperty = {}

-- Creates a property/argument/etc.
declare function gdproperty(params: GDPropertyParameters): GDProperty

type GDMethodMetadata = {
    args: {GDProperty}?,
    defaultArgs: {any}?,
    returnVal: GDProperty?,
    flags: number? -- MethodFlags
}

type GDClassDefinition = {
    -- Tool? (default: false)
    Tool: (self: GDClassDefinition, isTool: boolean) -> (),

    -- Initialization: takes the Object and internal table
    Initialize: (self: GDClassDefinition, func: (any, any) -> ()) -> (),

    -- Subscribe to a notification
    Subscribe: (
        self: GDClassDefinition,
        notification: number,
        handler: (...any) -> ()
    ) -> (),

    -- Register a method
    RegisterMethod: (
        self: GDClassDefinition,
        name: string,
        method: (self: any, ...any) -> any,
        metadata: GDMethodMetadata?
    ) -> (),

    -- Register a property (exported)
    RegisterProperty: (
        self: GDClassDefinition,
        property: GDProperty,
        getter: string?,
        setter: string?,
        default: any?
    ) -> ()

    -- TODO: Signals, RPCs
}

-- Creates a class definition
-- default extends: RefCounted
declare function gdclass(className: string, extends: string?): GDClassDefinition
