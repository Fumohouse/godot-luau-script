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
    luau_api["global_enums"] = godot_api["global_enums"]
    luau_api["builtin_classes"] = [
        bc
        for bc in godot_api["builtin_classes"]
        if bc["name"] not in ["Nil", "bool", "int", "float"]
    ]
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

    # Classes
    luau_api["classes"] = []
    for g_class in godot_api["classes"]:
        name = g_class["name"]
        g_class_settings = class_settings.get(name)
        g_methods_settings = g_class_settings and g_class_settings.get("methods")

        l_class = g_class.copy()

        l_class["permissions"] = (
            g_class_settings and g_class_settings.get("default_permissions")
        ) or get_class_default_permissions(g_class, godot_api)

        if "methods" in g_class:
            l_class["methods"] = []
            for method in g_class["methods"]:
                method_name_luau = utils.snake_to_pascal(method["name"])
                g_method_settings = g_methods_settings and g_methods_settings.get(
                    method_name_luau
                )

                l_method = method.copy()

                l_method["permissions"] = (
                    g_method_settings and g_method_settings.get("permissions")
                ) or "INHERIT"

                l_class["methods"].append(l_method)

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
