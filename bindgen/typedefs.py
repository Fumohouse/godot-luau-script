from . import constants, utils
from .utils import append


gd_luau_type_map = {
    "bool": "boolean",
    "int": "number",
    "float": "number",
    "String": "string",
}

# Types that should have *Like suffix when used as an argument since it can be
# coerced from other types
gd_luau_arg_coerce_types = ["StringName", "NodePath"]


def get_luau_type(type_string, api, is_ret=False, is_obj_nullable=True):
    if type_string in gd_luau_arg_coerce_types and not is_ret:
        return type_string + "Like"

    if type_string in gd_luau_type_map:
        return gd_luau_type_map[type_string]

    if type_string.startswith(constants.typed_array_prefix):
        array_type_name = type_string.split(":")[-1]
        return (
            f"TypedArray<{get_luau_type(array_type_name, api, is_obj_nullable=False)}>"
        )

    enum_name = None

    if type_string.startswith(constants.enum_prefix):
        enum_name = type_string[len(constants.enum_prefix) :]

    if type_string.startswith(constants.bitfield_prefix):
        enum_name = type_string[len(constants.bitfield_prefix) :]

    if enum_name:
        if enum_name == "Variant.Type":
            return "EnumVariantType"

        if enum_name.find(".") == -1:
            return "Enum" + enum_name

        enum_segments = enum_name.split(".")
        return f"ClassEnum{enum_segments[0]}_{enum_segments[1]}"

    if is_obj_nullable and type_string in [c["name"] for c in api["classes"]]:
        type_string = type_string + "?"

    return type_string


def generate_args(method, api, with_self=True, is_type=False, self_annot=""):
    arguments = method.get("arguments")
    is_vararg = method.get("is_vararg")

    out = ""

    if with_self:
        out += "self"

        if self_annot != "":
            out += ": " + self_annot

    if arguments:
        if with_self:
            out += ", "

        for i, arg in enumerate(arguments):
            # The p helps to escape collisions with Luau reserved keywords (e.g. "end")
            # .strip(): one argument name has a leading space for some reason
            arg_name = "p" + utils.snake_to_pascal(arg["name"].strip())
            arg_type = get_luau_type(arg["type"], api)

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


def generate_method(
    src, class_name, method, api, is_def_static=False, is_obj_nullable=True
):
    method_name = utils.snake_to_pascal(method["name"])

    # Special cases
    if class_name == "Resource" and method_name == "Duplicate":
        append(src, 1, "Duplicate: <T>(self: T) -> T")
        return

    # Other
    method_ret_str = ""

    if "return_type" in method:
        method_ret_str = get_luau_type(
            method["return_type"], api, True, is_obj_nullable
        )
    elif "return_value" in method:
        method_ret_str = get_luau_type(
            method["return_value"]["type"], api, True, is_obj_nullable
        )

    if is_def_static:
        is_method_static = "is_static" in method and method["is_static"]

        if method_ret_str == "":
            method_ret_str = "()"

        append(
            src,
            1,
            f"{method_name}: ({generate_args(method, api, not is_method_static, True, get_luau_type(class_name, api, is_obj_nullable=False))}) -> {method_ret_str}",
        )
    else:
        if method_ret_str != "":
            method_ret_str = ": " + method_ret_str

        append(
            src,
            1,
            f"function {method_name}({generate_args(method, api)}){method_ret_str}",
        )


def generate_enum(src, enum, class_name=""):
    # Based on https://github.com/JohnnyMorganz/luau-lsp/blob/main/scripts/globalTypes.d.lua
    name = enum["name"]

    type_name = "Enum" + name
    if class_name != "":
        type_name = f"ClassEnum{class_name}_{name}"

    # Item class
    src.append(f"export type {type_name} = number\n")

    # Enum type
    internal_type_name = type_name + "_INTERNAL"
    src.append(f"declare class {internal_type_name}")

    for value in enum["values"]:
        append(src, 1, f"{value["name"]}: {type_name}")

    src.append("end")

    return name, type_name, internal_type_name


def generate_builtin_class(src, builtin_class, api):
    name = builtin_class["name"]
    src.append(f"-- {name}")

    #
    # Class declaration
    #

    if name != "String":
        src.append(f"declare class {name}")

        has_set_method = name.endswith("Array") or name == "Dictionary"
        # Keying
        if builtin_class["is_keyed"]:
            append(src, 1, "function Get(self, key: Variant): Variant")
            if has_set_method:
                append(src, 1, "function Set(self, key: Variant, value: Variant)")

        # Indexing
        indexing_type_name = builtin_class.get("indexing_return_type")
        if indexing_type_name:
            append(
                src,
                1,
                f"function Get(self, key: number): {get_luau_type(indexing_type_name, api, True)}",
            )
            if has_set_method:
                append(
                    src,
                    1,
                    f"function Set(self, key: number, value: {get_luau_type(indexing_type_name, api)})",
                )

        # Members
        for member in builtin_class.get("members", []):
            member_name = member["name"]
            member_type = get_luau_type(member["type"], api, True)

            append(src, 1, f'["{member_name}"]: {member_type}')

        # Methods
        for method in builtin_class.get("methods", []):
            if method["is_static"]:
                continue

            generate_method(src, name, method, api)

        # Operators
        for op in builtin_class.get("operators", []):
            op_mt_name = op["luau_name"]
            op_return_type = get_luau_type(op["return_type"], api, True)

            if "right_type" in op:
                op_right_type = get_luau_type(op["right_type"], api)

                append(
                    src,
                    1,
                    f"function {op_mt_name}(self, other: {op_right_type}): {op_return_type}",
                )
            else:
                append(src, 1, f"function {op_mt_name}(self): {op_return_type}")

        # Special cases
        if name.endswith("Array"):
            append(
                src,
                1,
                """\
function __len(self): number
function __iter(self): any\
""",
            )
        elif name == "Dictionary":
            append(src, 1, "function __iter(self): any")

        src.append("end\n")

    #
    # Global declaration
    #

    # Enum type definitions
    class_enum_types = []

    for enum in builtin_class.get("enums", []):
        enum_name, type_name, internal_type_name = generate_enum(src, enum, name)
        class_enum_types.append((enum_name, type_name, internal_type_name))

        src.append("")

    # Main declaration
    src.append(f"declare class {name}_GLOBAL extends ClassGlobal")

    # Enums
    for enum_name, type_name, internal_type_name in class_enum_types:
        append(src, 1, f"{enum_name}: {internal_type_name}")

    # Constants
    for constant in builtin_class.get("constants", []):
        constant_name = constant["name"]
        constant_type = get_luau_type(constant["type"], api, True)

        append(src, 1, f"{constant_name}: {constant_type}")

    # Constructors
    if name == "Callable":
        append(src, 1, "new: (obj: Object, method: string) -> Callable")
    elif name != "String":
        for constructor in builtin_class["constructors"]:
            append(
                src,
                1,
                f"new: ({generate_args(constructor, api, False, True)}) -> {name}",
            )

    # Statics
    for method in builtin_class.get("methods", []):
        generate_method(src, name, method, api, True)

    src.append(
        f"""\
end

declare {name}: {name}_GLOBAL
"""
    )


def generate_class(src, g_class, api):
    name = g_class["name"]
    src.append(f"-- {name}")

    #
    # Class declaration
    #

    extends_str = " extends " + g_class["inherits"] if "inherits" in g_class else ""

    src.append(f"declare class {name}{extends_str}")

    # Methods
    for method in g_class.get("methods", []):
        generate_method(src, name, method, api)

    # Custom Object methods
    if name == "Object":
        append(
            src,
            1,
            """\
function Set(self, key: string | StringName, value: Variant)
function Get(self, key: string | StringName): Variant
function IsA(self, type: string | GDClass | ClassGlobal): boolean
function Free(self)\
""",
        )

    # Signals
    for signal in g_class.get("signals", []):
        signal_name = utils.snake_to_camel(signal["name"])
        append(src, 1, f"{signal_name}: Signal")

    # Properties
    for prop in g_class.get("properties", []):
        setter = prop["setter"]
        getter = prop["getter"]
        if not setter and not getter:
            continue

        prop_name = utils.snake_to_camel(prop["name"])

        # BaseMaterial/ShaderMaterial multiple types
        prop_type = " | ".join(
            [get_luau_type(t, api, True) for t in prop["type"].split(",")]
        )

        append(src, 1, f'["{prop_name}"]: {prop_type}')

    src.append("end\n")

    #
    # Global declaration
    #

    # Enum type definitions
    class_enum_types = []

    for enum in g_class.get("enums", []):
        enum_name, type_name, internal_type_name = generate_enum(src, enum, name)
        class_enum_types.append((enum_name, type_name, internal_type_name))

        src.append("")

    src.append(f"declare class {name}_GLOBAL extends ClassGlobal")

    # Enums
    for enum_name, type_name, internal_type_name in class_enum_types:
        append(src, 1, f"{enum_name}: {internal_type_name}")

    # Constants
    for constant in g_class.get("constants", []):
        constant_name = constant["name"]
        append(src, 1, f"{constant_name}: number")

    # Constructor
    if g_class["is_instantiable"]:
        append(src, 1, f"new: () -> {name}")

    # Singleton
    if "singleton" in g_class:
        append(src, 1, f"singleton: {name}")

    # Statics
    for method in g_class.get("methods", []):
        generate_method(src, name, method, api, True)

    src.append(
        f"""\
end

declare {name}: {name}_GLOBAL
"""
    )


def generate_typedefs(defs_dir, api, lib_types, godot_types):
    src = [constants.header_comment_lua, ""]

    # Global enums
    global_enums = []

    src.append(
        """\
------------------
-- GLOBAL ENUMS --
------------------
"""
    )

    for enum in api["global_enums"]:
        enum_name = enum["name"]
        src.append(f"-- {enum_name}")

        name, type_name, internal_type_name = generate_enum(src, enum)
        global_enums.append((name, type_name, internal_type_name))

        src.append("")

    src.append("declare Enum: {")

    for name, type_name, internal_type_name in global_enums:
        append(src, 1, f"{name}: {internal_type_name},")

    append(src, 1, "Permissions: EnumPermissions_INTERNAL,")

    src.append("}\n")

    # Global constants
    src.append(
        """\
----------------------
-- GLOBAL CONSTANTS --
----------------------
"""
    )

    src.append("declare Constants: {")

    for constant in api["global_constants"]:
        constant_name = constant["name"]
        append(src, 1, f"{constant_name}: number,")

    src.append("}\n")

    # Utility functions
    src.append(
        """\
-----------------------
-- UTILITY FUNCTIONS --
-----------------------
"""
    )

    for func in api["utility_functions"]:
        func_name_luau = func["luau_name"]

        if func["is_print_func"]:
            src.append(f"declare function {func_name_luau}(...: any)")
        else:
            func_ret_str = (
                ": " + get_luau_type(func["return_type"], api, True)
                if "return_type" in func
                else ""
            )

            src.append(
                f"declare function {func_name_luau}({generate_args(func, api, False)}){func_ret_str}"
            )

    src.append("")

    # Builtin classes
    src.append(
        """\
---------------------
-- BUILTIN CLASSES --
---------------------

declare class ClassGlobal end
"""
    )

    builtin_classes = api["builtin_classes"]

    for builtin_class in builtin_classes:
        if builtin_class["name"] in ["StringName", "NodePath"]:
            continue

        generate_builtin_class(src, builtin_class, api)

    # Classes
    src.append(
        """\
-------------
-- CLASSES --
-------------
"""
    )

    classes = api["classes"]

    for g_class in classes:
        generate_class(src, g_class, api)

    # Special types
    src.append(
        """\
-------------------
-- SPECIAL TYPES --
-------------------
"""
    )

    var_def = "export type Variant = nil | boolean | number | string | Object | "

    for i, builtin_class in enumerate(builtin_classes):
        b_name = builtin_class["name"]
        if b_name == "String":
            continue

        var_def += b_name

        if i != len(builtin_classes) - 1:
            var_def += " | "

    src.append(var_def)

    # Type fragments
    with open(lib_types, "r") as f:
        src.append(f.read())

    with open(godot_types, "r") as f:
        src.append(f.read())

    # Save
    utils.write_file(defs_dir / "luauScriptTypes.gen.d.lua", src)
