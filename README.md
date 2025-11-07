# godot-luau-script (GLS)

[![Godot Badge](https://img.shields.io/badge/Godot-4.4--stable-orange)](https://godotengine.org/)
[![Code style: black](https://img.shields.io/badge/code%20style-black-000000.svg)](https://github.com/psf/black)

> **Note**
>
> This repository is mirrored from
> [Forgejo](https://git.seki.pw/Fumohouse/godot-luau-script) to
> [GitHub](https://github.com/Fumohouse/godot-luau-script). Issues and pull
> requests are accepted from both sites, but pushes only occur from Forgejo.

GDExtension for Godot 4 adding support for
[Luau](https://github.com/Roblox/luau), a variant of Lua 5.1, to Godot as a
scripting language.

[Documentation](https://ksk.codeberg.page/godot-luau-script/)

## Development status

godot-luau-script has been discontinued and superseded by
[shadowblox](https://git.seki.pw/Fumohouse/shadowblox).

In short, the added benefits of this project compared to GDScript (i.e.,
sandboxing) do not justify its maintenance burden and fracturing of development
between Luau and GDScript. If you are interested in forking or maintaining this
project, feel free to reach out. Outstanding tasks and ideas are listed in
`TASKS.md`.

shadowblox seeks to justify the maintenance burden of a GLS-like project by
implementing an existing interface, that being the Roblox DataModel. It carries
forward much of the code of GLS, while abstracting it away from Godot itself.
Visit the repository link above if you are interested.

## Recurring maintenance tasks

- Keeping up-to-date with GDExtension changes
- Areas that must be synced with Godot (`grep "! SYNC WITH"`)

## License

See `LICENSE.txt`.

Detailed information on the copyright of dependencies can be found in
`COPYRIGHT.txt`.
