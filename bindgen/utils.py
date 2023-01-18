import importlib.machinery
import importlib.util

from pathlib import Path
from . import constants


def load_cpp_binding_generator():
    # lol

    godot_cpp_path = Path(__file__).parent / \
        "../extern/godot-cpp/binding_generator.py"

    loader = importlib.machinery.SourceFileLoader(
        "binding_generator", str(godot_cpp_path))

    spec = importlib.util.spec_from_loader("binding_generator", loader)
    binding_generator = importlib.util.module_from_spec(spec)
    loader.exec_module(binding_generator)

    return binding_generator


binding_generator = load_cpp_binding_generator()


def write_file(path, lines):
    with path.open("w+") as file:
        file.write("\n".join(lines))


def should_skip_class(class_name):
    to_skip = ["Nil", "bool", "int", "float", "String"]

    return class_name in to_skip


def append(source, indent_level, line):
    lines = [
        constants.indent * indent_level + l if len(l) > 0 else ""
        for l in line.split("\n")
    ]
    source.append("\n".join(lines))


def snake_to_pascal(snake):
    segments = [s[0].upper() + s[1:] for s in snake.split("_") if len(s) > 0]
    output = "".join(segments)

    if (snake.startswith("_")):
        output = "_" + output

    return output.replace("2d", "2D").replace("3d", "3D")


def snake_to_camel(snake):
    pascal = snake_to_pascal(snake)

    begin_idx = [idx for idx, c in enumerate(
        pascal) if c.upper() != c.lower()][0]
    return pascal[:(begin_idx + 1)].lower() + pascal[(begin_idx + 1):]


def get_enum_name(enum_name):
    return enum_name.replace(".", "")


def get_enum_value_name(enum_name, value_name):
    special_prefixes = {
        "PropertyUsageFlags": "PROPERTY_USAGE_",
        "MethodFlags": "METHOD_FLAG_",
        "VariantType": "TYPE_",
        "VariantOperator": "OP_",

        # Class
        "PrimitiveType": "PRIMITIVE_"
    }

    enum_prefix = binding_generator.camel_to_snake(enum_name).upper() + "_"
    if enum_name in special_prefixes:
        enum_prefix = special_prefixes[enum_name]

    if value_name.startswith(enum_prefix):
        value_name = value_name[len(enum_prefix):]

    # Key codes
    if value_name[0].isdigit():
        value_name = "N" + value_name

    return value_name


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


def get_operators(operators):
    # since Variant is basically a catch-all type, comparison to Variant should always be last
    # otherwise the output could be unexpected
    # int is sorted after other types because it may be removed if a float operator exists
    def op_priority(op):
        if "right_type" not in op:
            return 0

        right_type = op["right_type"]
        if right_type == "int":
            return 1
        if right_type == "Variant":
            return 2

        return 0

    operators = sorted(operators, key=op_priority)

    # filter results
    output = []

    for op in operators:
        name = op["name"]

        # skip any unusable operators
        if name not in variant_op_map:
            continue

        if "right_type" in op:
            right_type = op["right_type"]

            # basically, if there was a float right_type previously then skip the int one
            if right_type == "int" and (True in [
                "right_type" in op and op["right_type"] == "float"
                for op in output
            ]):
                continue

        output.append(op)

    return output


def get_singletons(class_name, singletons):
    return [s for s in singletons if s["type"] == class_name]
