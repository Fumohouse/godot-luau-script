# Run with python3 -m bindgen.generate_permissions

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

for g_class in classes:
    class_name = g_class["name"]

    if "methods" in g_class:
        methods = [m for m in sorted(g_class["methods"], key=lambda m: m["name"]) if not m["is_virtual"]]
        if len(methods) == 0:
            continue

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

            check_class = find_by(classes, check_class["inherits"])

        print(f"default_permissions = \"{default_permissions}\" #")
        print("")

        print(f"[{class_name}.methods]")

        for method in methods:
            method_name = utils.snake_to_pascal(method["name"])
            print(f"{method_name} = \"INHERIT\" #")

        print("\n#############################\n")
