# Run with python3 -m bindgen.generate_class_settings

import json
from pathlib import Path
from . import utils

def find_by(items, name, key="name"):
    return [c for c in items if c[key] == name][0]


extension_api = Path(__file__).parent / \
    "../extern/godot-cpp/gdextension/extension_api.json"

api = {}

with open(extension_api, "r") as f:
    api = json.load(f)

classes = sorted(api["classes"], key=lambda c: c["name"])

print("""\
# This file is used by the binding generators to define class permissions and whether methods/properties are nullable.
# It must be updated on every Godot release.

# Notes:
# - Return values are "nullable" only if it is reasonably "likely" that the method will return a null value,
#   i.e. if the user should expect the value to be null.
#   In reality, almost every Object returned from Godot can be null, but at that point the nullability loses
#   much of its meaning and becomes annoying. And, in the end, assertions and using a nil value have the same
#   outcome of an error.
""")

for g_class in classes:
    class_name = g_class["name"]

    print(f"[{class_name}]")

    # Inherits
    if "inherits" in g_class:
        inherits = g_class["inherits"]
        print(f"inherits = \"{inherits}\"")

    # Default permissions
    default_permissions = "INTERNAL"

    child_defaults = {
        "Node": "BASE"
    }

    check_class = g_class
    while "inherits" in check_class:
        check_name = check_class["name"]
        if check_name in child_defaults:
            default_permissions = child_defaults[check_name]
            break
        elif check_name == "RefCounted":
            default_permissions = "BASE"
            break
        elif check_name.startswith("Editor"):
            # Don't really care what happens in the editor
            default_permissions = "BASE"
            break

        check_class = find_by(classes, check_class["inherits"])

    print(f"default_permissions = \"{default_permissions}\" #")
    print("")

    print(f"[{class_name}.methods]\n")

    if "methods" in g_class:
        methods = [m for m in sorted(utils.get_class_methods(g_class), key=lambda m: m["name"])]
        for method in methods:
            method_name = utils.snake_to_pascal(method["name"])
            print(f"[{class_name}.methods.{method_name}]")
            print("permissions = \"INHERIT\" #")
            if "return_value" in method and True in [c["name"] == method["return_value"]["type"] for c in api["classes"]]:
                print("ret_nullable = true #")
            print("")

    print("#############################\n")
