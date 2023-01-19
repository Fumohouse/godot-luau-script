from . import constants, utils
from .utils import append


gd_luau_type_map = {
    "bool": "boolean",
    "int": "number",
    "float": "number",
    "String": "string",
    "StringName": "string | StringName",
}


def get_luau_type(type_string):
    if type_string in gd_luau_type_map:
        return gd_luau_type_map[type_string]

    if type_string.startswith(constants.typed_array_prefix):
        array_type_name = type_string.split(":")[-1]
        return f"TypedArray<{get_luau_type(array_type_name)}>"

    enum_name = None

    if type_string.startswith(constants.enum_prefix):
        enum_name = type_string[len(constants.enum_prefix):]

    if type_string.startswith(constants.bitfield_prefix):
        enum_name = type_string[len(constants.bitfield_prefix):]

    if enum_name:
        if enum_name == "Variant.Type":
            return "EnumVariantType"

        if enum_name.find(".") == -1:
            return "Enum" + enum_name

        enum_segments = enum_name.split(".")
        return f"ClassEnum{enum_segments[0]}_{enum_segments[1]}"

    return type_string


def generate_args(method, with_self=True, is_type=False):
    is_vararg = method["is_vararg"] if "is_vararg" in method else False

    out = ""

    if with_self:
        out += "self"

    if "arguments" in method:
        arguments = method["arguments"]

        if with_self:
            out += ", "

        for i, arg in enumerate(arguments):
            # The p helps to escape collisions with Luau reserved keywords (e.g. "end")
            # .strip(): one argument name has a leadinng space for some reason
            arg_name = "p" + utils.snake_to_pascal(arg["name"].strip())
            arg_type = get_luau_type(arg["type"])

            out += f"{arg_name}: {arg_type}"

            if "default_value" in arg:
                out += "?"

            if (i != len(arguments) - 1) or is_vararg:
                out += ", "
    elif is_vararg:
        out += ", "

    if is_vararg:
        out += "...Variant" if is_type else "...: Variant"

    return out


def generate_method(src, method):
    method_name = utils.snake_to_pascal(method["name"])
    method_ret_str = ""

    if "return_type" in method:
        method_ret_str = get_luau_type(method["return_type"])
    elif "return_value" in method:
        method_ret_str = get_luau_type(method["return_value"]["type"])

    if "is_static" in method and method["is_static"]:
        if method_ret_str == "":
            method_ret_str = "()"

        append(
            src, 1, f"{method_name}: ({generate_args(method, False, True)}) -> {method_ret_str}")
    else:
        if method_ret_str != "":
            method_ret_str = ": " + method_ret_str

        append(
            src, 1, f"function {method_name}({generate_args(method)}){method_ret_str}")


def generate_enum(src, enum, class_name=""):
    # Based on https://github.com/JohnnyMorganz/luau-lsp/blob/main/scripts/globalTypes.d.lua

    orig_name = enum["name"]
    name = utils.get_enum_name(orig_name)
    values = enum["values"]

    type_name = "Enum" + name
    if class_name != "":
        type_name = f"ClassEnum{class_name}_{name}"

    # Item classes
    src.append(f"declare class {type_name} end")

    src.append("")

    # Enum type
    internal_type_name = type_name + "_INTERNAL"
    src.append(f"declare class {internal_type_name}")

    for value in values:
        value_name = utils.get_enum_value_name(name, value["name"])
        append(src, 1, f"{value_name}: {type_name}")

    src.append("end")

    return name, type_name, internal_type_name


def generate_builtin_class(src, builtin_class):
    name = builtin_class["name"]
    src.append(f"-- {name}")

    #
    # Class declaration
    #

    src.append(f"declare class {name}")

    # Keying
    if builtin_class["is_keyed"]:
        append(src, 1, """\
function __index(self, key: Variant): Variant
function __newindex(self, key: Variant, value: Variant)\
""")

    # Indexing
    if "indexing_return_type" in builtin_class:
        indexing_type_name = get_luau_type(
            builtin_class["indexing_return_type"])

        append(src, 1, f"""\
function __index(self, key: number): {indexing_type_name}
function __newindex(self, key: number, value: {indexing_type_name})\
""")

    # Members
    if "members" in builtin_class:
        for member in builtin_class["members"]:
            member_name = member["name"]
            member_type = get_luau_type(member["type"])

            append(src, 1, f"[\"{member_name}\"]: {member_type}")

    # Methods
    if "methods" in builtin_class:
        for method in builtin_class["methods"]:
            if method["is_static"]:
                continue

            generate_method(src, method)

    # Operators
    if "operators" in builtin_class:
        operators = utils.get_operators(name, builtin_class["operators"])

        for op in operators:
            op_mt_name = "__" + utils.variant_op_map[op["name"]]
            op_return_type = get_luau_type(op["return_type"])

            if "right_type" in op:
                op_right_type = get_luau_type(op["right_type"])

                append(
                    src, 1, f"function {op_mt_name}(self, other: {op_right_type}): {op_return_type}")
            else:
                append(
                    src, 1, f"function {op_mt_name}(self): {op_return_type}")

        # Special case: array operators
        if name.endswith("Array"):
            append(src, 1, """\
function __len(self): number
function __iter(self): any\
""")

    src.append("end\n")

    #
    # Global declaration
    #

    # Enum type definitions
    class_enum_types = []

    if "enums" in builtin_class:
        for enum in builtin_class["enums"]:
            enum_name, type_name, internal_type_name = generate_enum(
                src, enum, name)
            class_enum_types.append((
                enum_name, type_name, internal_type_name))

            src.append("")

    # Main declaration
    src.append(f"declare class {name}_GLOBAL")

    # Enums
    for enum_name, type_name, internal_type_name in class_enum_types:
        append(src, 1, f"{enum_name}: {internal_type_name}")

    # Constants
    if "constants" in builtin_class:
        for constant in builtin_class["constants"]:
            constant_name = constant["name"]
            constant_type = get_luau_type(constant["type"])

            append(src, 1, f"{constant_name}: {constant_type}")

    # Constructors
    if name == "Callable":
        append(src, 1, "new: (obj: Object, method: string) -> Callable")
    else:
        for constructor in builtin_class["constructors"]:
            append(
                src, 1, f"new: ({generate_args(constructor, False, True)}) -> {name}")

    # Statics
    if "methods" in builtin_class:
        for method in builtin_class["methods"]:
            if not method["is_static"]:
                continue

            generate_method(src, method)

    src.append(f"""\
end

declare {name}: {name}_GLOBAL
""")


def generate_class(src, g_class, singletons):
    name = g_class["name"]
    src.append(f"-- {name}")

    #
    # Class declaration
    #

    extends_str = " extends " + \
        g_class["inherits"] if "inherits" in g_class else ""

    src.append(f"declare class {name}{extends_str}")

    # Methods
    if "methods" in g_class:
        for method in g_class["methods"]:
            if method["is_static"] or method["is_virtual"]:
                continue

            generate_method(src, method)

    # Signals
    if "signals" in g_class:
        for signal in g_class["signals"]:
            signal_name = utils.snake_to_camel(signal["name"])
            append(src, 1, f"{signal_name}: Signal")

    # Properties
    if "properties" in g_class:
        for prop in g_class["properties"]:
            prop_name = utils.snake_to_camel(prop["name"])

            prop_type = prop["type"]

            # Enum properties are registered as integers, so reference their setter/getter to find real type
            if "methods" in g_class:
                prop_method_ref = prop["getter"] if "getter" in prop else prop["setter"]
                prop_method = [m for m in g_class["methods"] if m["name"] == prop_method_ref]

                if len(prop_method) == 1:
                    prop_method = prop_method[0]

                    if "return_value" in prop_method:
                        prop_type = prop_method["return_value"]["type"]
                    else:
                        arg_idx = 0 if "index" not in prop else 1
                        prop_type = prop_method["arguments"][arg_idx]["type"]

            # BaseMaterial/ShaderMaterial multiple types
            prop_type = " | ".join([get_luau_type(t)
                                   for t in prop_type.split(",")])

            append(src, 1, f"[\"{prop_name}\"]: {prop_type}")

    src.append("end\n")

    #
    # Global declaration
    #

    # Enum type definitions
    class_enum_types = []

    if "enums" in g_class:
        for enum in g_class["enums"]:
            enum_name, type_name, internal_type_name = generate_enum(
                src, enum, name)
            class_enum_types.append((enum_name, type_name, internal_type_name))

            src.append("")

    src.append(f"declare class {name}_GLOBAL")

    # Enums
    for enum_name, type_name, internal_type_name in class_enum_types:
        append(src, 1, f"{enum_name}: {internal_type_name}")

    # Constants
    if "constants" in g_class:
        for constant in g_class["constants"]:
            constant_name = constant["name"]
            append(src, 1, f"{constant_name}: number")

    # Constructor
    if g_class["is_instantiable"]:
        append(src, 1, f"new: () -> {name}")

    # Singleton
    singleton_matches = utils.get_singletons(name, singletons)
    if len(singleton_matches) > 0:
        append(src, 1, f"function GetSingleton(self): {name}")

    # Statics
    if "methods" in g_class:
        for method in g_class["methods"]:
            if not method["is_static"]:
                continue

            generate_method(src, method)

    src.append(f"""\
end

declare {name}: {name}_GLOBAL
""")


def generate_typedefs(defs_dir, api):
    src = [constants.header_comment_lua, ""]

    # Global enums
    global_enums = []

    src.append("""\
------------------
-- GLOBAL ENUMS --
------------------
""")

    for enum in api["global_enums"]:
        enum_name = enum["name"]
        src.append(f"-- {enum_name}")

        name, type_name, internal_type_name = generate_enum(
            src, enum)
        global_enums.append((name, type_name, internal_type_name))

        src.append("")

    src.append("declare Enum: {")

    for name, type_name, internal_type_name in global_enums:
        append(src, 1, f"{name}: {internal_type_name},")

    src.append("}\n")

    # Global constants
    src.append("""\
----------------------
-- GLOBAL CONSTANTS --
----------------------
""")

    src.append("declare Constants: {")

    for constant in api["global_constants"]:
        constant_name = constant["name"]
        append(src, 1, f"{constant_name}: number,")

    src.append("}\n")

    # Utility functions
    src.append("""\
-----------------------
-- UTILITY FUNCTIONS --
-----------------------
""")

    for func in api["utility_functions"]:
        func_name = func["name"]
        if func_name not in utils.utils_to_bind:
            continue

        func_name = utils.utils_to_bind[func_name] if utils.utils_to_bind[func_name] else func_name
        func_ret_str = ": " + \
            get_luau_type(func["return_type"]) if "return_type" in func else ""

        src.append(
            f"declare function {func_name}({generate_args(func, False)}){func_ret_str}")

    src.append("")

    # Builtin classes
    src.append("""\
---------------------
-- BUILTIN CLASSES --
---------------------
""")

    builtin_classes = api["builtin_classes"]

    for builtin_class in builtin_classes:
        if utils.should_skip_class(builtin_class["name"]):
            continue

        generate_builtin_class(src, builtin_class)

    # Classes
    src.append("""\
-------------
-- CLASSES --
-------------
""")

    classes = api["classes"]

    def sort_classes(class_list):
        def key_func(g_class):
            check_class = g_class
            parent_count = 0

            while True:
                if "inherits" not in check_class:
                    return parent_count

                check_class = [c for c in class_list if c["name"]
                               == check_class["inherits"]][0]
                parent_count += 1

        return key_func

    classes = sorted(classes, key=sort_classes(classes))

    for g_class in classes:
        generate_class(src, g_class, api["singletons"])

    # Special types
    src.append("""\
-------------------
-- SPECIAL TYPES --
-------------------
""")

    var_def = "export type Variant = nil | boolean | number | string | Object | "

    for i, builtin_class in enumerate(builtin_classes):
        if utils.should_skip_class(builtin_class["name"]):
            continue

        var_def += builtin_class["name"]

        if i != len(builtin_classes) - 1:
            var_def += " | "

    src.append(var_def)

    # TODO: better way?
    src.append("export type TypedArray<T> = Array")

    # luau_lib types
    src.append("""
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
    function Tool(self, isTool: boolean): GDClassDefinition
    function Permissions(self, permissions: EnumPermissions): GDClassDefinition
    function IconPath(self, path: string): GDClassDefinition
    RegisterImpl: <T>(self: GDClassDefinition, table: T) -> GDClassDefinition & T

    function RegisterMethod(self, name: string): GDMethod
    function RegisterProperty(self, name: string, propertyOrType: EnumVariantType | GDProperty): GDClassProperty
    function RegisterSignal(self, name: string): GDSignal
    function RegisterRpc(self, rpcConfig: GDRpcConfig)
    function RegisterConstant(self, name: string, value: Variant)

    function __newindex(self, key: string, value: any)
    function __index(self, key: string): any
end

declare function gdclass(name: string?, extends: string?): GDClassDefinition

declare function wait(duration: number): number
""")

    # Save
    utils.write_file(defs_dir / "luauScriptTypes.gen.d.lua", src)
