import json
import platform
from . import utils

major, minor, patch = platform.python_version_tuple()

if int(major) > 3 or (int(major) == 3 and int(minor) >= 11):
    import tomllib
else:
    import tomli as tomllib

utils_to_bind = {
    # math functions not provided by Luau
    "ease": (None, False),
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


def get_enum(enum):
    values = enum["values"]

    l_enum = enum.copy()
    l_enum["name"] = enum["name"].replace(".", "")
    l_enum["values"] = []

    # Find prefix (defined as common preceding letters with underscore at end)
    enum_prefix = ""

    first_val_name = values[0]["name"]

    i = 0
    while True:
        if i >= len(first_val_name):
            break

        candidate_letter = first_val_name[i]
        invalid_count = 0
        for value in values:
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

    for val in values:
        # As of 4.2-beta1, Godot has exactly two uint64_t enum values.
        # They don't seem that important so they are ignored for now. Sorry if you need them.
        if val["value"] < -2147483648 or val["value"] > 2147483647:
            continue

        l_value = val.copy()
        value_name = val["name"]

        # Find value
        if value_name.startswith(enum_prefix):
            value_name = value_name[len(enum_prefix) :]

        if value_name[0].isdigit():
            # Key codes, etc.
            value_name = "N" + value_name

        l_value["name"] = value_name

        l_enum["values"].append(l_value)

    return l_enum


def check_class_settings(class_settings, godot_api):
    for class_name in class_settings:
        found_class = [c for c in godot_api["classes"] if c["name"] == class_name]
        assert len(found_class) == 1, f"class {class_name} does not exist!"

        g_class = found_class[0]

        if "methods" in class_settings[class_name]:
            assert "methods" in g_class, f"class {class_name} has no methods!"

            for method_name in class_settings[class_name]["methods"]:
                found_method = [
                    m
                    for m in g_class["methods"]
                    if utils.snake_to_pascal(m["name"]) == method_name
                ]
                assert (
                    len(found_method) == 1
                ), f"method {class_name}.{method_name} does not exist!"


def get_builtin_methods(b_class):
    def should_skip(method):
        if b_class["name"] == "String" and method["name"] in [
            # Remove methods that are almost entirely redundant.
            # Some versions that use Godot types instead of Lua types are kept.
            "length",
            "is_empty",
            "to_lower",
            "to_upper",
            "to_int",
            "to_float",
            "is_valid_int",
            "is_valid_float",
            "repeat",
            "left",
            "right",
            "contains",
            "format",
        ]:
            return True

        return False

    return [m for m in b_class["methods"] if not should_skip(m)]


variant_op_map = {
    "==": "eq",
    "<": "lt",
    "<=": "le",
    "+": "add",
    "-": "sub",
    "*": "mul",
    "/": "div",
    "%": "mod",
    "unary-": "unm",
}


def get_builtin_operators(b_class):
    # since Variant is basically a catch-all type, comparison to Variant should always be last
    # otherwise the output could be unexpected
    # int is sorted after other types because it may be removed if a float operator exists
    def op_priority(op):
        right_type = op.get("right_type")

        if not right_type:
            return 0
        if right_type == "int":
            return 1
        if right_type == "Variant":
            return 2

        return 0

    operators = sorted(b_class["operators"], key=op_priority)

    output = []

    for op in operators:
        op = op.copy()
        name = op["name"]
        luau_name = variant_op_map.get(name)
        if not luau_name:
            # Skip any unusable operators
            continue

        right_type = op.get("right_type")
        if right_type:
            # Luau does not support __eq between objects that aren't the same type
            if name == "==" and right_type != b_class["name"]:
                continue

            # basically, if there was a float right_type previously then skip the int one
            if right_type == "int" and (
                True
                in ["right_type" in op and op["right_type"] == "float" for op in output]
            ):
                continue

        op["luau_name"] = "__" + luau_name
        output.append(op)

    return output


def get_class_default_permissions(g_class, godot_api):
    child_defaults = {"Node": "BASE"}

    check_class = g_class

    if check_class["is_refcounted"] or check_class["api_type"] == "editor":
        # RefCounted is generally safe
        # Don't really care what happens in the editor
        return "BASE"

    classes = godot_api["classes"]
    while "inherits" in check_class:
        check_name = check_class["name"]
        if check_name in child_defaults:
            return child_defaults[check_name]

        check_class = [
            c for c in godot_api["classes"] if c["name"] == check_class["inherits"]
        ][0]

    return "INTERNAL"


def get_class_methods(g_class, g_class_settings):
    def should_skip(method):
        # doesn't make sense to support virtuals
        # (can't call them, and Luau script instances will receive calls to these methods for free if implemented)
        if method["is_virtual"]:
            return True

        # Handled as special case
        if g_class["name"] == "Object" and method["name"] in ["get", "set", "is_class"]:
            return True

        # Not of any concern to Luau
        if "arguments" in method and (
            True in [arg["type"] == "const void*" for arg in method["arguments"]]
        ):
            return True

        return False

    g_methods_settings = g_class_settings and g_class_settings.get("methods")
    output = []

    for method in g_class["methods"]:
        if should_skip(method):
            continue

        method_name_luau = utils.snake_to_pascal(method["name"])
        g_method_settings = g_methods_settings and g_methods_settings.get(
            method_name_luau
        )

        l_method = method.copy()

        l_method["permissions"] = (
            g_method_settings and g_method_settings.get("permissions")
        ) or "INHERIT"

        output.append(l_method)

    return output


def get_property_setget(prop, g_class, api):
    setter = prop.get("setter")
    getter = prop.get("getter")

    def has_setget(method_name, chk_class):
        # Currently, no funny business with inheriters having the method
        in_class = "methods" in chk_class and True in [
            m["name"] == method_name for m in chk_class["methods"]
        ]

        # Check base class (e.g. InputEventMouseMotion::pressed -> getter is InputEvent::is_pressed)
        if not in_class and "inherits" in chk_class:
            base_class = [
                c for c in api["classes"] if c["name"] == chk_class["inherits"]
            ][0]
            return has_setget(method_name, base_class)

        return in_class

    def get_actual_setget(method_name):
        # Attempt to strip _ to ensure any virtual setters/getters have the correct method name
        if not method_name:
            return "", False

        if has_setget(method_name, g_class):
            return method_name, False
        elif has_setget(method_name.strip("_"), g_class):
            return method_name.strip("_"), False
        else:
            # This is possible and expected (for now); see https://github.com/godotengine/godot/issues/64429
            return "", True

    setter, setter_not_found = get_actual_setget(setter)
    getter, getter_not_found = get_actual_setget(getter)

    return setter, getter, setter_not_found, getter_not_found


def get_class_properties(g_class, api):
    output = []

    for prop in g_class["properties"]:
        l_prop = prop.copy()
        setter, getter, setter_not_found, getter_not_found = get_property_setget(
            prop, g_class, api
        )

        if setter_not_found:
            setter = prop["setter"]
            print(f"INFO: setter not found: {g_class["name"]}::{setter}")

        if getter_not_found:
            getter = prop["getter"]
            print(f"INFO: getter not found: {g_class["name"]}::{getter}")

        l_prop["setter"] = setter
        l_prop["getter"] = getter

        # Enum properties are registered as integers, so reference their setter/getter to find real type
        if "methods" in g_class:
            prop_method_ref = getter or setter
            prop_method = [
                m for m in g_class["methods"] if m["name"] == prop_method_ref
            ]

            if len(prop_method) == 1:
                prop_method = prop_method[0]

                if "return_value" in prop_method:
                    l_prop["type"] = prop_method["return_value"]["type"]
                else:
                    arg_idx = 0 if "index" not in prop else 1
                    l_prop["type"] = prop_method["arguments"][arg_idx]["type"]

        output.append(l_prop)

    return output


def emit_files(target, source, env):
    in_files = [
        env.File("extern/godot-cpp/gdextension/extension_api.json"),
        env.File("bindgen/class_settings.toml"),
    ]

    out_files = [env.File("gen/luau_api.json")]

    env.Clean(out_files, target)

    return target + out_files, source + in_files


def generate_bindings(target, source, env):
    #
    # Inputs
    #

    godot_api = None
    with open(str(source[0])) as api_file:
        godot_api = json.load(api_file)

    class_settings = {}
    with open(str(source[1]), "rb") as settings_file:
        class_settings = tomllib.load(settings_file)

    check_class_settings(class_settings, godot_api)

    #
    # Outputs
    #

    luau_api = {}

    # Direct copy
    luau_api["global_constants"] = godot_api["global_constants"]
    luau_api["global_enums"] = list(map(get_enum, godot_api["global_enums"]))
    luau_api["singletons"] = godot_api["singletons"]

    # Utility functions
    luau_api["utility_functions"] = []
    for func in godot_api["utility_functions"]:
        name = func["name"]
        if name not in utils_to_bind:
            continue

        l_func = func.copy()
        luau_name, is_print_func = utils_to_bind[name]
        l_func["luau_name"] = luau_name or name
        l_func["is_print_func"] = is_print_func

        luau_api["utility_functions"].append(l_func)

    # Builtins
    luau_api["builtin_classes"] = []
    for b_class in godot_api["builtin_classes"]:
        name = b_class["name"]
        if name in ["Nil", "bool", "int", "float"]:
            continue

        l_class = b_class.copy()

        if "enums" in b_class:
            l_class["enums"] = list(map(get_enum, b_class["enums"]))

        if "methods" in b_class:
            l_class["methods"] = get_builtin_methods(b_class)

        if "operators" in b_class:
            l_class["operators"] = get_builtin_operators(b_class)

        luau_api["builtin_classes"].append(l_class)

    # Classes
    luau_api["classes"] = []
    for g_class in godot_api["classes"]:
        name = g_class["name"]
        g_class_settings = class_settings.get(name)

        l_class = g_class.copy()

        l_class["permissions"] = (
            g_class_settings and g_class_settings.get("default_permissions")
        ) or get_class_default_permissions(g_class, godot_api)

        singleton_matches = [s for s in godot_api["singletons"] if s["type"] == name]
        if len(singleton_matches) > 0:
            l_class["singleton"] = singleton_matches[0]["name"]

        if "enums" in g_class:
            l_class["enums"] = list(map(get_enum, g_class["enums"]))

        if "methods" in g_class:
            l_class["methods"] = get_class_methods(g_class, g_class_settings)

        if "properties" in g_class:
            l_class["properties"] = get_class_properties(g_class, godot_api)

        luau_api["classes"].append(l_class)

    #
    # Post processing
    #

    # Sort by inheritance (mainly for typedefs)
    def sort_classes(g_class):
        check_class = g_class
        parent_count = 0

        while True:
            if "inherits" not in check_class:
                return parent_count

            check_class = [
                c for c in luau_api["classes"] if c["name"] == check_class["inherits"]
            ][0]
            parent_count += 1

    luau_api["classes"] = sorted(luau_api["classes"], key=sort_classes)

    with open(target[0].abspath, "w") as f:
        json.dump(luau_api, f, indent="\t")
