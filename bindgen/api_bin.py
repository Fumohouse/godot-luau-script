from . import constants, utils, godot

from io import BytesIO
import struct

# ! SYNC WITH core/extension/extension_api_dump.cpp

#########
# Utils #
#########


def write_uint8(io, i):
    io.write(struct.pack("<B", i))


def write_bool(io, b):
    write_uint8(io, 1 if b else 0)


def write_int32(io, i):
    io.write(struct.pack("<i", i))


def write_size(io, i):
    write_int32(io, i)


def write_uint32(io, i):
    io.write(struct.pack("<I", i))


def write_int64(io, i):
    io.write(struct.pack("<q", i))


def write_uint64(io, i):
    io.write(struct.pack("<Q", i))


def write_string(io, string):
    if len(string) == 0:
        write_uint64(io, 0)
        return

    write_uint64(io, len(string) + 1)
    io.write(string.encode("utf-8"))
    io.write(bytes([0x00]))


##########
# Common #
##########

ThreadPermissions = {
    "INHERIT": -1,
    "BASE": 0,
    "INTERNAL": 1 << 0,
    "OS": 1 << 1,
    "FILE": 1 << 2,
    "HTTP": 1 << 3,
}


def is_object_type(type_name, classes):
    return True in [c["name"] == type_name for c in classes]


def get_variant_idx(value, variant_values, variant_value_map):
    if value.isdigit():
        # For MSVC (will use unsigned long if not)
        value = f"(uint64_t){value}"

    if value in variant_value_map:
        return variant_value_map[value]

    variant_value_map[value] = len(variant_values)
    variant_values.append(value)

    return variant_value_map[value]


def filter_methods(methods):
    inst_methods = [method for method in methods if not method["is_static"]]
    static_methods = [method for method in methods if method["is_static"]]

    return inst_methods, static_methods


def generate_enum(io, enum):
    # ApiEnum
    enum_name = utils.get_enum_name(enum["name"])
    write_string(io, enum_name)  # String name

    is_bitfield = enum["is_bitfield"] if "is_bitfield" in enum else False
    write_bool(io, is_bitfield)  # bool is_bitfield

    values = utils.get_enum_values(enum)
    write_size(io, len(values))  # size num_values

    # Pair<String, int32_t> values[num_values]
    for value in values:
        value_name = utils.get_enum_value_name(enum, value["name"])

        write_string(io, value_name)  # String name
        write_int32(io, value["value"])  # int32_t value


def generate_constant(io, constant):
    # ApiConstant
    write_string(io, constant["name"])  # String name
    write_int64(io, constant["value"])  # int64_t value


def generate_argument_no_default(io, argument):
    # ApiArgumentNoDefault
    write_string(io, argument["name"])  # String name
    write_uint32(io, godot.get_variant_type(argument["type"]))  # Variant::Type type


#####################
# Utility functions #
#####################


def generate_utility_function(io, utility_function):
    # ApiUtilityFunction
    name = utility_function["name"]
    luau_name, is_print_func = utils.utils_to_bind[name]
    luau_name = luau_name if luau_name else name

    write_string(io, luau_name)  # String luau_name
    write_string(io, name)  # String gd_name
    write_string(io, f"Godot.UtilityFunctions.{name}")  # String debug_name
    write_bool(io, utility_function["is_vararg"])  # bool is_vararg
    write_bool(io, is_print_func)  # bool is_print_func

    write_uint32(io, utility_function["hash"])  # uint32_t hash

    arguments = utility_function.get("arguments")
    write_size(io, len(arguments))  # size num_args

    # ApiArgumentNoDefault args[num_args]
    for argument in arguments:
        generate_argument_no_default(io, argument)

    write_int32(
        io,
        (
            godot.get_variant_type(utility_function["return_type"])
            if "return_type" in utility_function
            else -1
        ),
    )  # int32_t return_type


###################
# Builtin classes #
###################


def generate_builtin_class_method(
    io, method, class_name, metatable_name, variant_values, variant_value_map
):
    # ApiVariantMethod
    name = method["name"]
    is_vararg = method["is_vararg"]
    is_static = method["is_static"]
    is_const = method["is_const"]

    name_luau = utils.snake_to_pascal(name)
    debug_name = (
        f"{class_name}.{name_luau}" if is_static else f"{metatable_name}.{name_luau}"
    )

    write_string(io, name_luau)  # String name
    write_string(io, name)  # String gd_name
    write_string(io, debug_name)  # String debug_name

    write_bool(io, is_vararg)  # bool is_vararg
    write_bool(io, is_static)  # bool is_static
    write_bool(io, is_const)  # bool is_const

    write_uint32(io, method["hash"])  # uint32_t hash

    arguments = method.get("arguments", [])
    write_size(io, len(arguments))  # size num_args

    # ApiArgument args[num_args]
    for argument in arguments:
        write_string(io, argument["name"])  # String name
        # Variant::Type type
        write_uint32(io, godot.get_variant_type(argument["type"]))

        default_value = argument.get("default_value")
        if default_value:
            if default_value == "null":
                default_value = "Variant()"

            # int32_t default_variant_index
            write_int32(
                io,
                get_variant_idx(default_value, variant_values, variant_value_map),
            )
        else:
            write_int32(io, -1)  # int32_t default_variant_index

    if "return_type" in method:
        # int32_t return_type
        write_int32(io, godot.get_variant_type(method["return_type"]))
    else:
        write_int32(io, -1)  # int32_t return_type


def ctor_help_string(class_name, constructors):
    lines = []

    for constructor in constructors:
        args_str = ""
        arguments = constructor.get("arguments", [])

        for argument in arguments:
            args_str += argument["name"] + ": " + argument["type"]

            if argument != arguments[-1]:
                args_str += ", "

        lines.append(f"\t- {class_name}({args_str})")

    return "no constructors matched. expected one of the following:\n" + "\n".join(
        lines
    )


def generate_builtin_class(io, builtin_class, variant_values, variant_value_map):
    # ApiBuiltinClass
    class_name = builtin_class["name"]
    metatable_name = constants.builtin_metatable_prefix + class_name

    write_string(io, class_name)  # String name
    write_string(io, metatable_name)  # String metatable_name

    variant_type = godot.get_variant_type(class_name)
    write_uint32(io, variant_type)  # Variant::Type type

    write_bool(io, builtin_class["is_keyed"])  # bool is_keyed

    # int32_t indexing_return_type
    if "indexing_return_type" in builtin_class:
        write_int32(io, godot.get_variant_type(builtin_class["indexing_return_type"]))
    else:
        write_int32(io, -1)

    # enums
    enums = builtin_class.get("enums", [])
    write_size(io, len(enums))  # size num_enums

    for enum in enums:  # ApiEnum enums[num_enums]
        generate_enum(io, enum)

    # constants
    class_constants = builtin_class.get("constants", [])
    write_size(io, len(class_constants))  # size num_constants

    # ApiVariantConstant constants[num_constants]
    for constant in class_constants:
        write_string(io, constant["name"])  # String name

    # constructors
    if class_name != "Callable":  # Special case
        constructors = builtin_class["constructors"]
        write_size(io, len(constructors))  # size num_constructors

        # ApiVariantConstructor ctors[num_constructors]
        for constructor in constructors:
            # ApiVariantConstructor
            arguments = constructor.get("arguments", [])
            write_size(io, len(arguments))  # size num_args

            # ApiArgumentNoDefault args[num_args]
            for argument in arguments:
                generate_argument_no_default(io, argument)

        # String constructor_debug_name
        write_string(io, f"{class_name}.new")
        # String constructor_error_string
        write_string(io, ctor_help_string(class_name, constructors))
    else:
        write_size(io, 0)  # size num_constructors

    # members
    members = builtin_class.get("members", [])
    write_uint32(io, len(members))  # uint32_t num_members

    for member in members:
        write_string(io, member["name"])  # String name
        write_uint32(io, godot.get_variant_type(member["type"]))  # Variant::Type type

    # String newindex_debug_name
    write_string(io, f"{metatable_name}.__newindex")
    write_string(io, f"{metatable_name}.__index")  # String index_debug_name

    # methods
    methods = utils.get_builtin_methods(builtin_class)
    inst_methods, static_methods = filter_methods(methods)

    # instance methods
    write_uint32(io, len(inst_methods))  # uint32_t num_instance_methods
    # ApiVariantMethod instance_methods[num_instance_methods]
    for method in inst_methods:
        generate_builtin_class_method(
            io,
            method,
            class_name,
            metatable_name,
            variant_values,
            variant_value_map,
        )

    # String namecall_debug_name
    write_string(io, f"{metatable_name}.__namecall")

    # static methods
    write_size(io, len(static_methods))  # size num_static_methods
    # ApiVariantMethod static_methods[num_static_methods]
    for method in static_methods:
        generate_builtin_class_method(
            io,
            method,
            class_name,
            metatable_name,
            variant_values,
            variant_value_map,
        )

    # operators
    operators = utils.get_operators(class_name, builtin_class)
    operators_map = {}

    for variant_op in operators:
        name = variant_op["name"]

        right_type = None
        right_type = 0
        if "right_type" in variant_op:
            right_type = godot.get_variant_type(variant_op["right_type"])

        return_type = godot.get_variant_type(variant_op["return_type"])

        if name not in operators_map:
            operators_map[name] = []

        operators_map[name].append(
            {
                "metatable_name": "__" + utils.variant_op_map[name],
                "right_type": right_type,
                "return_type": return_type,
            }
        )

    write_uint32(io, len(operators_map))  # uint32_t num_operator_types

    for variant_op_name, ops in operators_map.items():
        # Variant::Operator op
        write_uint32(io, godot.get_variant_op(variant_op_name))
        write_size(io, len(ops))  # size num_operators

        for op in ops:
            # Variant::Type right_type
            write_uint32(io, op["right_type"])
            # Variant::Type return_type
            write_uint32(io, op["return_type"])

        op_metatable_name = op["metatable_name"]
        # String debug_name
        write_string(io, f"{metatable_name}.{op_metatable_name}")


###########
# Classes #
###########


def generate_class_type(io, type_string, classes):
    # ApiClassType
    variant_type = -1
    type_name = ""
    is_enum = False
    is_bitfield = False

    if type_string == "Nil":
        pass
    elif is_object_type(type_string, classes):
        variant_type = godot.get_variant_type("Object")
        type_name = type_string
    elif type_string.startswith(constants.typed_array_prefix):
        variant_type = godot.get_variant_type("Array")
        type_name = type_string.split(":")[-1]
    elif type_string.startswith(constants.enum_prefix):
        variant_type = godot.get_variant_type("int")
        type_name = type_string[len(constants.enum_prefix) :]
        is_enum = True
    elif type_string.startswith(constants.bitfield_prefix):
        variant_type = godot.get_variant_type("int")
        type_name = type_string[len(constants.bitfield_prefix) :]
        is_bitfield = True
    else:
        variant_type = godot.get_variant_type(type_string)
        type_name = type_string

    write_int32(io, variant_type)  # int32_t type
    write_string(io, type_name)  # String type_name
    write_bool(io, is_enum)  # bool is_enum
    write_bool(io, is_bitfield)  # bool is_bitfield

    return variant_type, type_name


def generate_class_argument(io, argument, classes, variant_values, variant_value_map):
    # ApiClassArgument
    write_string(io, argument["name"])  # String name

    variant_type, type_name = generate_class_type(
        io, argument["type"], classes
    )  # ApiClassType type

    if "default_value" in argument:
        default_value = argument["default_value"]

        if default_value == "null":
            if variant_type == godot.get_variant_type("Variant"):
                default_value = "Variant()"
            else:
                default_value = "nullptr"
        elif (
            default_value == ""
            and variant_type != godot.get_variant_type("Variant")
            and variant_type != godot.get_variant_type("Object")
        ):
            default_value = f"{type_name}()"
        elif variant_type == godot.get_variant_type("Array"):
            if default_value == "[]" or (
                type_name != "" and default_value.endswith("([])")
            ):
                default_value = f"Array()"
        elif default_value == "{}" and variant_type == godot.get_variant_type(
            "Dictionary"
        ):
            default_value = f"Dictionary()"
        elif default_value == '&""' and variant_type == godot.get_variant_type(
            "StringName"
        ):
            default_value = f"StringName()"

        index = get_variant_idx(default_value, variant_values, variant_value_map)
        write_int32(io, index)  # int32_t default_variant_index
    else:
        write_int32(io, -1)  # int32_t default_variant_index


def generate_class_method(
    io,
    class_name,
    metatable_name,
    classes,
    method,
    variant_values,
    variant_value_map,
):
    # ApiClassMethod
    method_name = method["name"]
    is_const = method["is_const"]
    is_static = method["is_static"]
    is_vararg = method["is_vararg"]

    method_name_luau = utils.snake_to_pascal(method_name)
    method_debug_name = (
        f"{class_name}.{method_name_luau}"
        if is_static
        else f"{metatable_name}.{method_name_luau}"
    )

    write_string(io, method_name_luau)  # String name
    write_string(io, method_name)  # String gd_name
    write_string(io, method_debug_name)  # String debug_name

    # permissions
    # ThreadPermissions permissions
    write_int32(io, ThreadPermissions[method["permissions"]])

    # more properties
    write_bool(io, is_const)  # bool is_const
    write_bool(io, is_static)  # bool is_static
    write_bool(io, is_vararg)  # bool is_vararg

    write_uint32(io, method["hash"])  # uint32_t hash

    # args
    arguments = method.get("arguments", [])
    write_size(io, len(arguments))  # size num_args

    # ApiClassArgument args[num_args]
    for argument in arguments:
        generate_class_argument(
            io, argument, classes, variant_values, variant_value_map
        )

    # return
    # ApiClassType return_type
    if "return_value" in method:
        generate_class_type(io, method["return_value"]["type"], classes)
    else:
        generate_class_type(io, "Nil", classes)


def generate_class(io, g_class, classes, singletons, variant_values, variant_value_map):
    # ApiClass
    class_name = g_class["name"]
    metatable_name = constants.class_metatable_prefix + class_name

    write_string(io, class_name)  # String name
    write_string(io, metatable_name)  # String metatable_name

    # inheritance
    if "inherits" in g_class:
        inherits = g_class["inherits"]
        parent_idx_result = [
            idx for idx, c in enumerate(classes) if c["name"] == inherits
        ]

        write_int32(io, parent_idx_result[0])  # int32_t parent_idx
    else:
        write_int32(io, -1)  # int32_t parent_idx

    # permissions
    # ThreadPermissions default_permissions
    write_int32(io, ThreadPermissions[g_class["permissions"]])

    # enums
    enums = g_class.get("enums", [])
    write_size(io, len(enums))  # size num_enums

    for enum in enums:  # ApiEnum enums[num_enums]
        generate_enum(io, enum)

    # constants
    class_constants = g_class.get("constants", [])
    write_size(io, len(class_constants))  # size num_constants

    # ApiConstant constants[num_constants]
    for constant in class_constants:
        generate_constant(io, constant)

    # constructor
    is_instantiable = g_class["is_instantiable"]
    write_bool(io, is_instantiable)  # bool is_instantiable
    # String constructor_debug_name
    write_string(io, f"{metatable_name}.new" if is_instantiable else "")

    # methods
    methods = utils.get_class_methods(g_class)
    inst_methods, static_methods = filter_methods(methods)

    # instance methods
    write_uint32(io, len(inst_methods))  # uint32_t num_methods

    # ApiClassMethod methods[num_methods]
    for method in inst_methods:
        generate_class_method(
            io,
            class_name,
            metatable_name,
            classes,
            method,
            variant_values,
            variant_value_map,
        )

    if len(inst_methods) > 0:
        # String namecall_debug_name
        write_string(io, f"{metatable_name}.__namecall")

    # static methods
    write_size(io, len(static_methods))  # size num_static_methods

    # ApiClassMethod static_methods[num_static_methods]
    for method in static_methods:
        generate_class_method(
            io,
            class_name,
            metatable_name,
            classes,
            method,
            variant_values,
            variant_value_map,
        )

    # signals
    signals = g_class.get("signals", [])
    write_uint32(io, len(signals))  # uint32_t num_signals

    # ApiClassSignal signals[num_signals]
    for signal in signals:
        signal_name = signal["name"]
        signal_name_luau = utils.snake_to_camel(signal_name)

        write_string(io, signal_name_luau)  # String name
        write_string(io, signal_name)  # String gd_name

        signal_args = signal.get("arguments", [])
        write_size(io, len(signal_args))  # size num_args

        # ApiClassArgument args[num_args]
        for argument in signal_args:
            generate_class_argument(
                io, argument, classes, variant_values, variant_value_map
            )

    # properties
    properties = g_class.get("properties", [])
    write_uint32(io, len(properties))  # uint32_t num_properties

    # ApiClassProperty properties[num_properties]
    for prop in properties:
        name_luau = utils.snake_to_camel(prop["name"])

        write_string(io, name_luau)  # String name

        # Ignore them for now; property types are not even checked in the bindings.
        # https://github.com/godotengine/godot/pull/88349
        types = [t for t in prop["type"].split(",") if not t.startswith("-")]
        write_size(io, len(types))  # size num_types
        for t in types:  # ApiClassType types[num_types]
            generate_class_type(io, t, classes)

        setter, getter, setter_not_found, getter_not_found = utils.get_property_setget(
            prop, g_class, classes
        )

        if setter_not_found:
            setter = prop["setter"]
            print(f"INFO: setter not found: {class_name}::{setter}")

        if getter_not_found:
            getter = prop["getter"]
            print(f"INFO: getter not found: {class_name}::{getter}")

        write_string(io, utils.snake_to_pascal(setter))  # String setter
        write_string(io, utils.snake_to_pascal(getter))  # String getter

        write_int32(io, prop.get("index", -1))  # int32_t index

    # String newindex_debug_name
    write_string(io, f"{metatable_name}.__newindex")
    # String index_debug_name
    write_string(io, f"{metatable_name}.__index")

    # singleton
    singleton_matches = utils.get_singletons(class_name, singletons)

    singleton = ""
    if len(singleton_matches) > 0:
        singleton = singleton_matches[0]["name"]

    write_string(io, singleton)  # String singleton


########
# Main #
########


def generate_api_bin(src_dir, api):
    ###################
    # Generate binary #
    ###################
    """
    - Little endian
    - All strings are utf-8, null-terminated, and their length includes the null terminator

    Common shorthands:
    - String:
        - uint64_t len
        - char str[len]
    - bool: uint8_t
    - size: int32_t
    - Variant::Type: uint32_t
    - Variant::Operator: uint32_t
    - ThreadPermissions: int32_t
    """

    api_bin = BytesIO()

    # Store const/default values as a const Variant array
    variant_values = []
    variant_value_map = {}

    # Global enums
    global_enums = api["global_enums"]
    write_size(api_bin, len(global_enums))  # size num_enums

    for enum in global_enums:  # ApiEnum enums[num_enums]
        generate_enum(api_bin, enum)

    # Global constants
    global_constants = api["global_constants"]
    write_size(api_bin, len(global_constants))  # size num_constants

    for constant in global_constants:  # ApiConstant constants[num_constants]
        generate_constant(api_bin, constant)

    # Utility functions
    utility_functions = [
        uf for uf in api["utility_functions"] if uf["name"] in utils.utils_to_bind
    ]
    # size num_utility_functions
    write_size(api_bin, len(utility_functions))

    # ApiUtilityFunction utility_functions[num_utility_functions]
    for utility_function in utility_functions:
        generate_utility_function(api_bin, utility_function)

    # Builtin classes
    builtin_classes = [
        bc
        for bc in api["builtin_classes"]
        if not bc["name"] in ["StringName", "NodePath"]
    ]
    write_size(api_bin, len(builtin_classes))  # size num_builtin_classes

    # ApiBuiltinClasses builtin_classes[num_builtin_classes]
    for builtin_class in builtin_classes:
        generate_builtin_class(
            api_bin, builtin_class, variant_values, variant_value_map
        )

    # Classes
    classes = api["classes"]
    singletons = api["singletons"]

    write_size(api_bin, len(classes))  # size num_classes

    for g_class in classes:
        generate_class(
            api_bin,
            g_class,
            classes,
            singletons,
            variant_values,
            variant_value_map,
        )

    ###################
    # Generate source #
    ###################

    src = [constants.header_comment, ""]

    bytes_str = ",".join([hex(b) for b in bytearray(api_bin.getvalue())])

    # put in a separate source file to avoid upsetting C++ language servers
    src.append(
        f"""\
#include "core/extension_api.h"

#include <cstdint>
#include <gdextension_interface.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

static_assert(GDEXTENSION_VARIANT_TYPE_VARIANT_MAX == {len(godot.VariantType)}, "variant type list in api_bin is outdated");
static_assert(GDEXTENSION_VARIANT_OP_MAX == {len(godot.VariantOperator)}, "variant operator list in api_bin is outdated");

// a static const variable would result in initialization before the Variant type itself is ready
const Variant &get_variant_value(int p_idx) {{
    static const Variant variant_values[] = {{{",".join(variant_values)}}};
    return variant_values[p_idx];
}}

const uint8_t api_bin[] = {{{bytes_str}}};
const uint64_t api_bin_length = {api_bin.getbuffer().nbytes};
"""
    )

    # Save
    utils.write_file(src_dir / "extension_api_bin.gen.cpp", src)
