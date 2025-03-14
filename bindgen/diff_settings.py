import tomllib
import json

class_settings = {}
with open("class_settings.toml", "rb") as settings_file:
    class_settings = tomllib.load(settings_file)

orig_settings = {}
with open("class_settings_orig.toml", "rb") as settings_file:
    orig_settings = tomllib.load(settings_file)

new_settings = {}

for c in class_settings:
    new = {"methods": {}}
    keep = False

    c1 = class_settings[c]
    c2 = orig_settings[c]

    if c1["default_permissions"] != c2["default_permissions"]:
        new["default_permissions"] = c1["default_permissions"]
        keep = True

    for m in c1["methods"]:
        m1 = c1["methods"][m]
        m2 = c2["methods"][m]

        if m1["permissions"] != m2["permissions"]:
            new["methods"][m] = m1
            keep = True

    if keep:
        new_settings[c] = new

print(json.dumps(new_settings))
