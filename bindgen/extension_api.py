from . import constants, utils
from .utils import append

binding_generator = utils.load_cpp_binding_generator()


# see Variant::get_type_name
def cpp_type_to_variant_type(cpp_type):
    prefix = "GDEXTENSION_VARIANT_TYPE_"

    if cpp_type == "Variant":
        return prefix + "NIL"

    return prefix + binding_generator.camel_to_snake(cpp_type).upper()


def bool_to_cpp(b):
    return "true" if b else "false"


##########
# Common #
##########

def append_enum(src, indent_level, array, index, enum):
    special_prefixes = {
        "PropertyUsageFlags": "PROPERTY_USAGE_",
        "MethodFlags": "METHOD_FLAG_",
        "VariantType": "TYPE_",
        "VariantOperator": "OP_",
    }

    enum_name = enum["name"].replace(".", "")
    is_bitfield = ("is_bitfield" in enum) and enum["is_bitfield"]
    values = enum["values"]

    enum_prefix = binding_generator.camel_to_snake(enum_name).upper() + "_"
    if enum_name in special_prefixes:
        enum_prefix = special_prefixes[enum_name]

    append(src, indent_level, f"""\
{{ // {enum_name}
    ApiEnum new_enum;
    new_enum.name = "{enum_name}";
    new_enum.is_bitfield = {bool_to_cpp(is_bitfield)};

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


#####################
# Utility functions #
#####################

def append_utility_function(src, indent_level, index, utility_function, utils_to_bind):
    func_name = utility_function["name"]
    luau_name = utils_to_bind[func_name] if utils_to_bind[func_name] else func_name

    is_vararg = utility_function["is_vararg"]

    func_hash = utility_function["hash"]
    arguments = utility_function["arguments"]
    return_type = utility_function["return_type"] if "return_type" in utility_function else "Nil"

    append(src, indent_level, f"""\
{{ // {luau_name}
    ApiUtilityFunction func;
    func.name = "{luau_name}";
    func.debug_name = "Godot.UtilityFunctions.{luau_name}";
    func.is_vararg = {bool_to_cpp(is_vararg)};

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

    api.utility_functions.set({index}, func);
}} // {luau_name}\
""")


###################
# Builtin classes #
###################

def generate_ctor_help_literal(class_name, constructors):
    lines = []

    for constructor in constructors:
        args_str = ""

        if "arguments" in constructor:
            arguments = constructor["arguments"]

            for argument in arguments:
                args_str += argument["name"] + ": " + argument["type"]

                if argument != arguments[-1]:
                    args_str += ", "

        lines.append(f"- {class_name}({args_str})")

    return "\n".join([constants.indent + f"\"{line}\\n\"" for line in lines])


def append_builtin_class_method(src, indent_level, class_name, variant_type_name, metatable_name, set_call, method):
    method_name = method["name"]
    is_vararg = method["is_vararg"]
    is_static = method["is_static"]
    is_const = method["is_const"]

    method_hash = method["hash"]
    return_type = cpp_type_to_variant_type(
        method["return_type"]) if "return_type" in method else "-1"

    method_name_luau = utils.snake_to_pascal(method_name)
    method_debug_name = f"{class_name}.{method_name_luau}" if is_static else f"{metatable_name}.{method_name_luau}"

    append(src, indent_level, f"""\
{{
    ApiVariantMethod method;
    method.name = "{method_name_luau}";
    method.gd_name = "{method_name}";
    method.debug_name = "{method_debug_name}";
    method.is_vararg = {bool_to_cpp(is_vararg)};
    method.is_static = {bool_to_cpp(is_static)};
    method.is_const = {bool_to_cpp(is_const)};

    method.func = internal::gde_interface->variant_get_ptr_builtin_method({variant_type_name}, &method.gd_name, {method_hash});
    method.return_type = {return_type};
""")
    indent_level += 1

    if "arguments" in method:
        arguments = method["arguments"]

        append(src, indent_level,
               f"method.arguments.resize({len(arguments)});\n")

        for i, argument in enumerate(arguments):
            arg_name = argument["name"]
            arg_type_cpp = argument["type"]

            arg_type = cpp_type_to_variant_type(arg_type_cpp)

            append(src, indent_level, f"""\
{{
    ApiArgument arg;
    arg.name = "{arg_name}";
    arg.type = {arg_type};
""")
            indent_level += 1

            if "default_value" in argument:
                default_value = "Variant()" if argument["default_value"] == "null" else argument["default_value"]

                # for varargs, defaults are listed as a Variant (nil type)
                default_type = "GDEXTENSION_VARIANT_TYPE_NIL" if is_vararg else arg_type

                append(src, indent_level, f"""\
arg.has_default_value = true;

{{
    LuauVariant defval;
    defval.initialize({default_type});
    defval.assign_variant({default_value});

    arg.default_value = defval;
}}
""")

            indent_level -= 1
            append(src, indent_level, f"""\
    method.arguments.set({i}, arg);
}}
""")

    indent_level -= 1
    append(src, indent_level, f"""\
    {set_call.replace("%METHOD_NAME%", method_name_luau)}
}}
""")


def append_builtin_class(src, indent_level, index, builtin_class):
    class_name = builtin_class["name"]
    metatable_name = constants.builtin_metatable_prefix + class_name

    is_keyed = builtin_class["is_keyed"]
    indexing_return_type = builtin_class["indexing_return_type"] if "indexing_return_type" in builtin_class else None

    variant_type_name = cpp_type_to_variant_type(class_name)

    append(src, indent_level, f"""\
{{ // {class_name}
    ApiBuiltinClass new_class;
    new_class.name = "{class_name}";
    new_class.metatable_name = "{metatable_name}";
    new_class.type = {variant_type_name};

    new_class.constructor_debug_name = "{class_name}.__call";
    new_class.newindex_debug_name = "{metatable_name}.__newindex";
    new_class.index_debug_name = "{metatable_name}.__index";
    new_class.namecall_debug_name = "{metatable_name}.__namecall";
    new_class.global_index_debug_name = "{class_name}.__index";
""")
    indent_level += 1

    # Keyed/indexing setget
    if is_keyed:
        append(src, indent_level, f"""\
new_class.keyed_setter = internal::gde_interface->variant_get_ptr_keyed_setter({variant_type_name});
new_class.keyed_getter = internal::gde_interface->variant_get_ptr_keyed_getter({variant_type_name});
new_class.keyed_checker = internal::gde_interface->variant_get_ptr_keyed_checker({variant_type_name});
""")

    if indexing_return_type:
        indexing_return_type_cpp = cpp_type_to_variant_type(
            indexing_return_type)

        indexed_setter = "nullptr"  # e.g. for Vectors, index set is not allowed
        if class_name.endswith("Array"):
            indexed_setter = f"internal::gde_interface->variant_get_ptr_indexed_setter({variant_type_name})"

        append(src, indent_level, f"""\
new_class.indexing_return_type = {indexing_return_type_cpp};
new_class.indexed_setter = {indexed_setter};
new_class.indexed_getter = internal::gde_interface->variant_get_ptr_indexed_getter({variant_type_name});
""")

    # Enums
    if "enums" in builtin_class:
        enums = builtin_class["enums"]

        append(src, indent_level, f"""\
// Enums
new_class.enums.resize({len(enums)});
""")

        for i, enum in enumerate(enums):
            append_enum(src, indent_level, "new_class.enums", i, enum)
            src.append("")

    # Constants
    if "constants" in builtin_class:
        class_constants = builtin_class["constants"]

        append(src, indent_level, f"""\
// Constants
new_class.constants.resize({len(class_constants)});
""")

        for i, constant in enumerate(class_constants):
            constant_name = constant["name"]

            append(src, indent_level, f"""\
{{
    StringName const_name = "{constant_name}";
    Variant val;
    internal::gde_interface->variant_get_constant_value({variant_type_name}, &const_name, &val);

    new_class.constants.set({i}, {{"{constant_name}", val}});
}}
""")

    # Constructors
    constructors = builtin_class["constructors"]

    append(src, indent_level, f"""\
// Constructors
new_class.constructors.resize({len(constructors)});
new_class.constructor_error_string = "no constructors matched. expected one of the following:\\n"
{generate_ctor_help_literal(class_name, constructors)};
""")

    for i, constructor in enumerate(constructors):
        append(src, indent_level, f"""\
{{
    Vector<ApiArgumentNoDefault> arguments;\
""")
        indent_level += 1

        if "arguments" in constructor:
            ctor_args = constructor["arguments"]

            append(src, indent_level, f"arguments.resize({len(ctor_args)});")

            for j, ctor_arg in enumerate(ctor_args):
                arg_name = ctor_arg["name"]
                arg_type = cpp_type_to_variant_type(ctor_arg["type"])

                append(src, indent_level,
                       f"arguments.set({j}, {{\"{arg_name}\", {arg_type}}});")

        indent_level -= 1
        append(src, indent_level, f"""
    new_class.constructors.set({i}, {{
        internal::gde_interface->variant_get_ptr_constructor({variant_type_name}, {constructor["index"]}),
        arguments
    }});
}}
""")

    # Members
    if "members" in builtin_class:
        members = builtin_class["members"]

        append(src, indent_level, f"""\
// Members
new_class.members.reserve({len(members)});
""")

        for member in members:
            member_name = member["name"]
            member_type = cpp_type_to_variant_type(member["type"])

            member_name_luau = utils.snake_to_camel(member_name)

            append(src, indent_level, f"""\
{{
    StringName member_name = "{member_name}";

    new_class.members.insert("{member_name_luau}", {{
        "{member_name_luau}", {member_type},
        internal::gde_interface->variant_get_ptr_getter({variant_type_name}, &member_name)
    }});
}}
""")

    # Methods
    if "methods" in builtin_class:
        methods = builtin_class["methods"]

        inst_methods = [
            method for method in methods if not method["is_static"]]
        static_methods = [method for method in methods if method["is_static"]]

        append(src, indent_level, f"""\
// Methods
new_class.methods.reserve({len(inst_methods)});
new_class.static_methods.resize({len(static_methods)});
""")

        for method in inst_methods:
            append_builtin_class_method(src, indent_level, class_name, variant_type_name, metatable_name,
                                        "new_class.methods.insert(\"%METHOD_NAME%\", method);", method)

        for i, method in enumerate(static_methods):
            append_builtin_class_method(src, indent_level, class_name, variant_type_name,
                                        metatable_name, f"new_class.static_methods.set({i}, method);", method)

    # Operators
    if "operators" in builtin_class:
        operators_unsorted = builtin_class["operators"]

        variant_op_map = {
            "==": "eq",
            "<": "lt",
            "<=": "le",
            "+": "add",
            "-": "sub",
            "*": "mul",
            "/": "div",
            "%": "mod",
            "unary-": "unm"
        }

        def op_priority(op):
            if not ("right_type" in op):
                return 0

            right_type = binding_generator.correct_type(op["right_type"])
            if right_type == "Variant":
                return 1

            return 0

        # since Variant is basically a catch-all type, comparison to Variant should always be last
        # otherwise the output could be unexpected
        operators = sorted(operators_unsorted, key=op_priority)
        operators_map = {}

        for variant_op in operators:
            name = variant_op["name"]
            if name not in variant_op_map:
                continue

            right_type = cpp_type_to_variant_type(
                variant_op["right_type"]) if "right_type" in variant_op else "GDEXTENSION_VARIANT_TYPE_NIL"
            return_type = cpp_type_to_variant_type(
                variant_op["return_type"])

            variant_op_name = "GDEXTENSION_VARIANT_OP_" + \
                binding_generator.get_operator_id_name(name).upper()

            if variant_op_name not in operators_map:
                operators_map[variant_op_name] = []

            operators_map[variant_op_name].append({
                "metatable_name": "__" + variant_op_map[name],
                "right_type": right_type,
                "return_type": return_type
            })

        num_ops = len(operators_map)
        if class_name.endswith("Array"):
            num_ops += 1

        append(src, indent_level, f"""\
// Operators
new_class.operators.reserve({num_ops});
new_class.operator_debug_names.reserve({num_ops});
""")

        if class_name.endswith("Array"):
            append(src, indent_level, f"""\
{{
    // Array __len special case
    new_class.operators.insert(GDEXTENSION_VARIANT_OP_MAX, {{
        {{
            GDEXTENSION_VARIANT_TYPE_NIL,
            GDEXTENSION_VARIANT_TYPE_INT,
            [](GDExtensionConstTypePtr p_left, GDExtensionConstTypePtr, GDExtensionTypePtr r_result)
            {{
                *((int64_t *)r_result) = (({class_name} *)p_left)->size();
            }}
        }}
    }});

    new_class.operator_debug_names.insert(GDEXTENSION_VARIANT_OP_MAX, "{metatable_name}.__len");
}}
""")

        for variant_op_name, ops in operators_map.items():
            append(src, indent_level, f"""\
{{
    Vector<ApiVariantOperator> ops;
    ops.resize({len(ops)});
""")
            indent_level += 1

            for i, op in enumerate(ops):
                op_metatable_name = op["metatable_name"]
                right_type = op["right_type"]
                return_type = op["return_type"]

                right_type_name = right_type if right_type != "-1" else "GDEXTENSION_VARIANT_TYPE_NIL"

                append(src, indent_level, f"""\
ops.set({i}, {{
    {right_type},
    {return_type},
    internal::gde_interface->variant_get_ptr_operator_evaluator({variant_op_name}, {variant_type_name}, {right_type_name})
}});
""")

            indent_level -= 1
            append(src, indent_level, f"""\
    new_class.operators.insert({variant_op_name}, ops);
    new_class.operator_debug_names.insert({variant_op_name}, "{metatable_name}.{op_metatable_name}");
}}
""")

    indent_level -= 1
    append(src, indent_level, f"""\
    api.builtin_classes.set({index}, new_class);
}} // {class_name}\
""")


########
# Main #
########

def generate_extension_api(src_dir, api):
    src = [constants.header_comment, ""]

    src.append("""\
#include "extension_api.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "luagd_variant.h"

using namespace godot;

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
                       if not utils.should_skip_class(bc["name"])]
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
        append_utility_function(src, indent_level, i,
                                utility_function, utils_to_bind)

        if i != len(utility_functions) - 1:
            src.append("")

    indent_level -= 1
    append(src, indent_level, "} // utility functions\n")

    # Builtin classes
    append(src, indent_level, "{ // builtin classes")
    indent_level += 1

    for i, builtin_class in enumerate(builtin_classes):
        append_builtin_class(src, indent_level, i, builtin_class)

        if i != len(builtin_classes) - 1:
            src.append("")

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
