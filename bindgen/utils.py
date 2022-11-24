import importlib.machinery
import importlib.util

from pathlib import Path
from . import constants


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


def load_cpp_binding_generator():
    # lol

    godot_cpp_path = Path(__file__).parent / \
        "../extern/godot-cpp/binding_generator.py"

    loader = importlib.machinery.SourceFileLoader("binding_generator",
                                                  str(godot_cpp_path))

    spec = importlib.util.spec_from_loader("binding_generator", loader)
    binding_generator = importlib.util.module_from_spec(spec)
    loader.exec_module(binding_generator)

    return binding_generator


def snake_to_pascal(snake):
    segments = [s[0].upper() + s[1:] for s in snake.split("_") if len(s) > 0]
    output = "".join(segments)

    if (snake.startswith("_")):
        output = "_" + output

    return output


def snake_to_camel(snake):
    pascal = snake_to_pascal(snake)

    begin_idx = [idx for idx, c in enumerate(pascal) if c.upper() != c.lower()][0]
    return pascal[:(begin_idx + 1)].lower() + pascal[(begin_idx + 1):]
