# Introduction

`godot-luau-script` is a GDExtension adding support for Roblox's
[Luau](https://luau-lang.org/), a variant of Lua 5.1, to Godot 4.x as a
scripting language.

## Priorities

This project has a few specific goals:

- Parity with Godot's official scripting languages (GDScript, C#)
- Allow for (relatively) safe loading of user-generated content (UGC) including
  scripts
  - Sandbox "core" and "user" scripts from each other (separate Lua VMs)
  - Enforce permission levels for restricted APIs, such as networking or
    filesystem access
  - Limit breakage of the game by user scripts
  - Scan PCKs for unsafe script or resource access
- Support typechecking of source files

Note that `godot-luau-script` was specifically built for loading untrusted UGC.
As such, other languages like GDScript may be more suitable if UGC support is
not needed (that is, unless you are partial to Luau as a language).

## Disclaimer

This project is still in relatively early stages. Please keep in mind:

- **(Potentially major) breaking changes will happen often and whenever
  necessary.**
- This project will usually track the latest Godot version (including preview
  releases), and support for older versions is not planned.
- This documentation may be incomplete or out-of-date. Please [report an issue](https://github.com/Fumohouse/godot-luau-script/issues/new/choose)
  if this is the case.
- Some security issues may remain. Please report an issue if you encounter one.
