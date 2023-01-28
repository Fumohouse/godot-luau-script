--------------------
-- LUAU_LIB TYPES --
--------------------

declare class EnumPermissions end

declare class EnumPermissions_INTERNAL
    BASE: EnumPermissions
    INTERNAL: EnumPermissions
    OS: EnumPermissions
    FILE: EnumPermissions
    HTTP: EnumPermissions
end

declare EnumPermissions: EnumPermissions_INTERNAL

export type GDProperty = {
    type: EnumVariantType?,
    name: string?,
    hint: EnumPropertyHint?,
    hintString: string?,
    usage: EnumPropertyUsageFlags?,
    className: string?,
}

declare class GDMethod
    function Args(self, ...: GDProperty): GDMethod
    function DefaultArgs(self, ...: Variant): GDMethod
    function ReturnVal(self, val: GDProperty): GDMethod
    function Flags(self, flags: EnumMethodFlags): GDMethod
end

declare class GDClassProperty
    function Default(self, value: Variant): GDClassProperty
    function SetGet(self, setter: string?, getter: string?): GDClassProperty

    function Range(self, min: number, max: number, step: number?): GDClassProperty
    function Enum(self, ...: string): GDClassProperty
    function Suggestion(self, ...: string): GDClassProperty
    function Flags(self, ...: string): GDClassProperty
    function File(self, isGlobal: boolean, ...: string): GDClassProperty
    function Dir(self, isGlobal: boolean): GDClassProperty
    function Multiline(self): GDClassProperty
    function TextPlaceholder(self, placeholder: string): GDClassProperty
    function Flags2DRenderLayers(self): GDClassProperty
    function Flags2DPhysicsLayers(self): GDClassProperty
    function Flags2DNavigationLayers(self): GDClassProperty
    function Flags3DRenderLayers(self): GDClassProperty
    function Flags3DPhysicsLayers(self): GDClassProperty
    function Flags3DNavigationLayers(self): GDClassProperty
    function ExpEasing(self): GDClassProperty
    function NoAlpha(self): GDClassProperty
    function TypedArray(self, type: string, isResource: boolean?): GDClassProperty
    function Resource(self, type: string): GDClassProperty
    function NodePath(self, ...: string): GDClassProperty
end

declare class GDSignal
    function Args(self, ...: GDProperty): GDMethod
end

export type GDRpcConfig = {
    rpcMode: ClassEnumMultiplayerAPI_RPCMode,
    transferMode: ClassEnumMultiplayerPeer_TransferMode,
    callLocal: boolean,
    channel: number,
}

declare class GDClassDefinition
    new: () -> Object

    function Tool(self, isTool: boolean): GDClassDefinition
    function Permissions(self, permissions: EnumPermissions): GDClassDefinition
    function IconPath(self, path: string): GDClassDefinition
    RegisterImpl: <T>(self: GDClassDefinition, table: T) -> GDClassDefinition & T

    function RegisterMethod(self, name: string): GDMethod
    function RegisterMethodAST(self, name: string): GDMethod
    function RegisterProperty(self, name: string, propertyOrType: EnumVariantType | GDProperty): GDClassProperty
    function RegisterSignal(self, name: string): GDSignal
    function RegisterRpc(self, rpcConfig: GDRpcConfig)
    function RegisterConstant(self, name: string, value: Variant)

    function __newindex(self, key: string, value: any)
    function __index(self, key: string): any
end

declare function gdclass(name: string?, extends: string?): GDClassDefinition

-- TODO: constrain to Resource type?
declare function load<T>(path: string): T

declare function wait(duration: number): number

declare function SN(str: string): StringName
declare function NP(str: string): NodePath
