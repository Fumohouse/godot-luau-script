from .utils import append


def generate_enums(enums):
    src = []

    src.append("// Enums")

    for enum in enums:
        enum_values = enum["values"]

        src.append(f"lua_createtable(L, 0, {len(enum_values)});")

        for value in enum_values:
            src.append(f"""\
lua_pushinteger(L, {value["value"]});
lua_setfield(L, -2, "{value["name"]}");
""")

        src.append(f"""\
lua_setreadonly(L, -1, true);
lua_setfield(L, -3, "{enum["name"]}");
""")

    return "\n".join(src)
