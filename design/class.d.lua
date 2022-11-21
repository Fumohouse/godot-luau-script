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

type GDClassProperty = {
    property: GDProperty,
    getter: string?,
    setter: string?,
    default: any?
}

type GDMethodMetadata = {
    args: {GDProperty}?,
    defaultArgs: {any}?,
    returnVal: GDProperty?,
    flags: number? -- MethodFlags
}

type GDClassDefinition = {
    name: string?,
    extends: string?,
    tool: boolean?,
    methods: {[string]: GDMethodMetadata}?,
    properties: {[string]: GDClassProperty}?,
    [string]: any
}
