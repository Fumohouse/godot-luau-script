# Introduction

`godot-luau-script` is a GDExtension adding support for Roblox's
[Luau](https://luau-lang.org/), a variant of Lua 5.1, to Godot 4.x as a
scripting language.

## Priorities

This project has a few specific goals:

- Parity with Godot's official scripting languages (GDScript, C#)
- Allow for relatively safe loading of user-generated content (UGC) including
  scripts
  - Sandbox "core" and "user" scripts from each other (separate Lua VMs)
  - Enforce permission levels for restricted APIs, such as networking or
    filesystem access
  - Limit breakage of the game by user scripts
- Support typechecking of source files
- Be as easy as possible to learn and adopt for users who know Godot, Luau, or
  both

## Disclaimer

This project is still in relatively early stages. Please keep in mind:

- **Major breaking changes will happen often and whenever necessary.**
- This project will usually track the latest Godot version (including preview
  releases). Support for older versions is not planned.
- This documentation may be incomplete or out-of-date. Please report an issue
  ([GitHub](https://github.com/Fumohouse/godot-luau-script/issues/new/choose))
  if this is the case.
- Some security issues may remain. Please report an issue if you encounter one.
