# Introduction

`godot-luau-script` is a GDExtension adding support for Roblox's
[Luau](https://luau-lang.org/), a superset of Lua 5.1, to Godot 4.

It is developed mostly with [Fumohouse](https://github.com/Fumohouse/Fumohouse)
in mind, but it can be used in other projects.

## Priorities

This particular implementation has a few specific goals:

- Allow for (relatively) safe loading of user-generated content (UGC) including
  scripts
  - Sandbox "core" and "user" scripts from each other (separate Lua VMs)
  - Enforce permission levels for restricted APIs (like networking or filesystem
    access)
  - Limit breakage of the game by user scripts
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
- This documentation may be incomplete or out-of-date. Please
  [report an issue](https://github.com/Fumohouse/godot-luau-script/issues/new/choose)
  if this is the case.
- Building on any platform except for MSVC on Windows and LLVM on Linux is
  untested. Report an issue if you encounter issues building.
- Some editor functionality is still not implemented (mostly debugging,
  profiling, and analysis/autocomplete support) and won't be for some time.
- Some security issues may remain.
