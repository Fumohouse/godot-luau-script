def write_file(path, lines):
    with path.open("w+") as file:
        file.write("\n".join(lines))


def append(source, indent_level, line):
    lines = ["\t" * indent_level + l if len(l) > 0 else "" for l in line.split("\n")]
    source.append("\n".join(lines))


def snake_to_pascal(snake):
    segments = [s[0].upper() + s[1:] for s in snake.split("_") if len(s) > 0]
    output = "".join(segments)

    if snake.startswith("_"):
        output = "_" + output

    return output.replace("2d", "2D").replace("3d", "3D")


def snake_to_camel(snake):
    pascal = snake_to_pascal(snake)

    begin_idx = [idx for idx, c in enumerate(pascal) if c.upper() != c.lower()][0]
    return pascal[: (begin_idx + 1)].lower() + pascal[(begin_idx + 1) :]
