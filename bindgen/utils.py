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
    return class_name in ["Nil", "bool", "int", "float"]


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


def get_enum_value_name(enum, value_name):
    # Find prefix (defined as common preceding letters with underscore at end)
    enum_prefix = ""

    enum_values = enum["values"]
    first_val_name = enum_values[0]["name"]

    i = 0
    while True:
        if i >= len(first_val_name):
            break

        candidate_letter = first_val_name[i]
        invalid_count = 0
        for value in enum_values:
            val_name = value["name"]

            if i >= len(val_name) or val_name[i] != candidate_letter:
                invalid_count += 1

        # e.g. METHOD_FLAG_, METHOD_FLAGS_DEFAULT
        if invalid_count > 1:
            break

        enum_prefix += candidate_letter
        i += 1

    # Necessary because e.g. TRANSFER_MODE_UNRELIABLE, TRANSFER_MODE_UNRELIABLE_ORDERED common U
    while len(enum_prefix) > 0 and not enum_prefix.endswith("_"):
        enum_prefix = enum_prefix[:-1]

    # Find value
    if value_name.startswith(enum_prefix):
        value_name = value_name[len(enum_prefix):]

    if value_name[0].isdigit():
        # Key codes, etc.
        value_name = "N" + value_name

    return value_name


def get_enum_values(enum):
    def valid_enum_value(val):
        # As of 4.2-beta1, Godot has exactly two uint64_t enum values.
        # They don't seem that important so they are ignored for now. Sorry if you need them.
        if val["value"] < -2147483648 or val["value"] > 2147483647:
            return False

        return True

    return [val for val in enum["values"] if valid_enum_value(val)]


utils_to_bind = {
    # math functions not provided by Luau
    "ease": (None, False),
    "lerpf": ("lerp", False),
    "cubic_interpolate": (None, False),
    "bezier_interpolate": (None, False),
    "lerp_angle": (None, False),
    "inverse_lerp": (None, False),
    "range_lerp": (None, False),
    "smoothstep": (None, False),
    "move_toward": (None, False),
    "linear_to_db": (None, False),
    "db_to_linear": (None, False),
    "wrapf": ("wrap", False),
    "pingpong": (None, False),
    "is_equal_approx": (None, False),
    "is_zero_approx": (None, False),

    # print
    "print": (None, True),
    "printraw": (None, True),
    "printerr": (None, True),
    "print_verbose": (None, True),
    "print_rich": (None, True),
    "push_error": (None, True),
    "push_warning": (None, True),

    # variant
    "var_to_str": (None, False),
    "str_to_var": (None, False),
    "var_to_bytes": (None, False),
    "var_to_bytes_with_objects": (None, False),
    "bytes_to_var": (None, False),
    "bytes_to_var_with_objects": (None, False),

    # other
    "hash": (None, False),
    "is_instance_valid": (None, False),
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


def get_operators(class_name, operators):
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

            # Luau does not support __eq between objects that aren't the same type
            if name == "==" and right_type != class_name:
                continue

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


def get_builtin_methods(b_class):
    def should_skip(method):
        if b_class["name"] == "String" and method["name"] in [
            # Remove methods which are almost entirely redundant.
            # Some versions which use Godot types instead of Lua types are kept.
            "length", "is_empty",
            "to_lower", "to_upper",
            "to_int", "to_float",
            "is_valid_int", "is_valid_float",
            "repeat",
            "left", "right",
            "contains",
            "format",
        ]:
            return True

        return False

    return [m for m in b_class["methods"] if not should_skip(m)]


def get_class_methods(g_class):
    def should_skip(method):
        # doesn't make sense to support virtuals
        # (can't call them, and Luau script instances will receive calls to these methods for free if implemented)
        if method["is_virtual"]:
            return True

        # Handled as special case
        if g_class["name"] == "Object" and method["name"] in ["get", "set", "is_class"]:
            return True

        # Not of any concern to Luau
        if "arguments" in method and (True in [arg["type"] == "const void*" for arg in method["arguments"]]):
            return True

        return False

    return [m for m in g_class["methods"] if not should_skip(m)]


def get_property_setget(prop, g_class, classes):
    setter = prop["setter"] if "setter" in prop else ""
    getter = prop["getter"] if "getter" in prop else ""

    def has_setget(method_name, chk_class):
        # Currently, no funny business with inheriters having the method
        in_class = "methods" in chk_class and True in [m["name"] == method_name for m in chk_class["methods"]]

        # Check base class (e.g. InputEventMouseMotion::pressed -> getter is InputEvent::is_pressed)
        if not in_class and "inherits" in chk_class:
            base_class = [c for c in classes if c["name"] == chk_class["inherits"]][0]
            return has_setget(method_name, base_class)

        return in_class

    def get_actual_setget(method_name):
        # Attempt to strip _ to ensure any virtual setters/getters have the correct method name
        if method_name == "":
            return "", False

        if has_setget(method_name, g_class):
            return method_name, False
        elif has_setget(method_name.strip("_"), g_class):
            return method_name.strip("_"), False
        else:
            return "", True

    setter, setter_not_found = get_actual_setget(setter)
    getter, getter_not_found = get_actual_setget(getter)

    return setter, getter, setter_not_found, getter_not_found
