from . import constants, utils
from .utils import append

binding_generator = utils.load_cpp_binding_generator()


# see Variant::get_type_name
def cpp_type_to_variant_type(cpp_type):
    prefix = "GDEXTENSION_VARIANT_TYPE_"

    if cpp_type == "Variant":
        return prefix + "NIL"

    return prefix + binding_generator.camel_to_snake(cpp_type).upper()


def append_enum(src, indent_level, array, index, enum):
    special_prefixes = {
        "PropertyUsageFlags": "PROPERTY_USAGE_",
        "MethodFlags": "METHOD_FLAG_",
        "VariantType": "TYPE_",
        "VariantOperator": "OP_",
    }

    enum_name = enum["name"].replace(".", "")
    is_bitfield = "true" if (
        "is_bitfield" in enum) and enum["is_bitfield"] else "false"
    values = enum["values"]

    enum_prefix = binding_generator.camel_to_snake(enum_name).upper() + "_"
    if enum_name in special_prefixes:
        enum_prefix = special_prefixes[enum_name]

    append(src, indent_level, f"""\
{{ // {enum_name}
    ApiEnum new_enum;
    new_enum.name = "{enum_name}";
    new_enum.is_bitfield = {is_bitfield};

    new_enum.values.resize({len(values)});\
""")
    indent_level += 1

    for i, value in enumerate(values):
        value_name = value["name"]
        if value_name.startswith(enum_prefix):
            value_name = value_name[len(enum_prefix):]

        append(src, indent_level, f"""\
new_enum.values.set({i}, {{"{value_name}", {value["value"]}}});\
""")

    indent_level -= 1
    append(src, indent_level, f"""
    {array}.set({index}, new_enum);
}} // {enum_name}\
""")


def append_constant(src, indent_level, array, index, constant):
    name = constant["name"]
    value = constant["value"]

    append(src, indent_level,
           f"{array}.set({index}, {{\"{name}\", {value}}});")


def generate_extension_api(src_dir, api):
    src = [constants.header_comment, ""]

    src.append("""\
#include "extension_api.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/string_name.hpp>

const ExtensionApi &get_extension_api()
{
    static ExtensionApi api;
    static bool init = false;

    if (!init)
    {\
""")

    indent_level = 2

    # Lists of everything
    utils_to_bind = {
        # math functions not provided by Luau
        "ease": None,
        "lerpf": "lerp",
        "cubic_interpolate": None,
        "bezier_interpolate": None,
        "lerp_angle": None,
        "inverse_lerp": None,
        "range_lerp": None,
        "smoothstep": None,
        "move_toward": None,
        "linear2db": None,
        "db2linear": None,
        "wrapf": "wrap",
        "pingpong": None,

        # other
        "print": None,
        "print_rich": None,
        "printerr": None,
        "printraw": None,
        "print_verbose": None,
        "push_warning": "warn",
        "hash": None,
        "is_instance_valid": None,
    }

    global_enums = api["global_enums"]
    global_constants = api["global_constants"]
    utility_functions = [
        uf for uf in api["utility_functions"] if uf["name"] in utils_to_bind]
    builtin_classes = [bc for bc in api["builtin_classes"]
                       if not utils.should_skip_class(bc)]
    classes = api["classes"]

    # Initialize Vector sizes
    append(src, indent_level, f"""\
api.global_enums.resize({len(global_enums)});
api.global_constants.resize({len(global_constants)});
api.utility_functions.resize({len(utility_functions)});
api.builtin_classes.resize({len(builtin_classes)});
api.classes.resize({len(classes)});
""")

    # Global enums
    append(src, indent_level, "{ // global enums")
    indent_level += 1

    for i, global_enum in enumerate(global_enums):
        append_enum(src, indent_level, "api.global_enums", i, global_enum)

        if i != len(global_enums) - 1:
            src.append("")

    indent_level -= 1
    append(src, indent_level, "} // global enums\n")

    # Global constants
    append(src, indent_level, "{ // global constants")
    indent_level += 1

    for i, global_constant in enumerate(global_constants):
        append_constant(src, indent_level,
                        "api.global_constants", i, global_constant)

    indent_level -= 1
    append(src, indent_level, "} // global constants\n")

    # Utility functions
    append(src, indent_level, "{ // utility functions")
    indent_level += 1

    for i, utility_function in enumerate(utility_functions):
        func_name = utility_function["name"]
        luau_name = utils_to_bind[func_name] if utils_to_bind[func_name] else func_name

        is_vararg = "true" if utility_function["is_vararg"] else "false"

        func_hash = utility_function["hash"]
        arguments = utility_function["arguments"]
        return_type = utility_function["return_type"] if "return_type" in utility_function else "Nil"

        append(src, indent_level, f"""\
{{ // {luau_name}
    ApiUtilityFunction func;
    func.name = "{luau_name}";
    func.debug_name = "Godot.UtilityFunctions.{luau_name}";
    func.is_vararg = {is_vararg};

    StringName sn_name = "{func_name}";
    func.func = internal::gde_interface->variant_get_ptr_utility_function(&sn_name, {func_hash});

    func.arguments.resize({len(arguments)});
""")
        indent_level += 1

        for j, argument in enumerate(arguments):
            append(src, indent_level, f"""\
func.arguments.set({j}, {{"{argument["name"]}", {cpp_type_to_variant_type(argument["type"])}}});\
""")

        indent_level -= 1
        append(src, indent_level, f"""
    func.return_type = {cpp_type_to_variant_type(return_type)};

    api.utility_functions.set({i}, func);
}} // {luau_name}\
""")

        if i != len(utility_functions) - 1:
            src.append("")

    indent_level -= 1
    append(src, indent_level, "} // utility functions\n")

    # Builtin classes
    append(src, indent_level, "{ // builtin classes")
    indent_level += 1

    indent_level -= 1
    append(src, indent_level, "} // builtin classes\n")

    # Classes
    append(src, indent_level, "{ // classes")
    indent_level += 1

    indent_level -= 1
    append(src, indent_level, "} // classes\n")

    indent_level += 1
    src.append("""\
        init = true;
    }

    return api;
}\
""")

    # Save
    utils.write_file(src_dir / "extension_api.gen.cpp", src)
