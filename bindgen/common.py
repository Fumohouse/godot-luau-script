from . import utils
from .ptrcall import generate_arg_required

binding_generator = utils.load_cpp_binding_generator()


def get_luau_type(type_name, api):
    if True in [c["name"] == type_name for c in api["classes"]]:
        return type_name + " *"

    if type_name.startswith("enum::") or type_name.startswith("bitfield::"):
        return "uint32_t"

    return binding_generator.correct_type(type_name)

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


def generate_method_args(class_name, method, api):
    src = []

    is_static = method["is_static"]
    is_vararg = method["is_vararg"]

    arg_start_index = 1
    required_argc = 0

    self_name = "nullptr"

    # Get self
    if not is_static:
        arg_start_index = 2
        decl, arg_name, varcall = generate_arg_required(
            "self", class_name, 1, api
        )

        self_name = arg_name

        src.append(decl + "\n")

    # Define arg lists
    src.append(f"""\
int argc = lua_gettop(L);

Vector<GDNativeTypePtr> args;
args.resize(argc - {arg_start_index - 1});
""")

    if is_vararg:
        src.append(f"""\
Vector<Variant> varargs;
varargs.resize(argc - {arg_start_index - 1});
""")

    # Get required args
    if "arguments" in method:
        arguments = method["arguments"]
        required_argc = len(arguments)

        arg_index = arg_start_index

        for argument in arguments:
            decl, arg_name, varcall = generate_arg_required(
                # for some reason one method name has a space at the beginning
                "p_" + argument["name"].strip(), argument["type"], arg_index, api)

            src.append(decl)

            if is_vararg:
                vararg_index = arg_index - arg_start_index

                src.append(f"""\
varargs.set({vararg_index}, {varcall});
args.set({vararg_index}, const_cast<Variant *>(&varargs[{vararg_index}]));
""")
            else:
                src.append(f"args.set({arg_index - arg_start_index}, {arg_name});\n")

            arg_index += 1

    # Get varargs
    if is_vararg:
        src.append(f"""\
for (int i = {((arg_start_index - 1) + required_argc) + 1}; i <= argc; i++)
{{
    varargs.set(i - {arg_start_index}, LuaStackOp<Variant>::get(L, i));
    args.set(i - {arg_start_index}, const_cast<Variant *>(&varargs[i - {arg_start_index}]));
}}
""")

    return "\n".join(src), self_name
