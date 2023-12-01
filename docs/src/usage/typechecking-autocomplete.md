# Typechecking and Autocomplete

Upon building, `godot-luau-script` will generate a single type definition file
for use by Luau's analyzer.

Autocomplete and analysis in the Godot editor is not supported. As such, the
most viable setup is to use Visual Studio Code and the [luau-lsp](https://github.com/JohnnyMorganz/luau-lsp)
extension.

## Setup

After installing VSCode and luau-lsp, you will need to do the
following setup:

- Set the preference `luau-lsp.require.mode` to `relativeToFile` to make
  `require` relative to the current script.
- Set the preference `luau-lsp.types.roblox` to `false` to disable Roblox types
  being loaded by default.
- Set the preference `luau-lsp.types.definitionFiles` to an array containing the
  path to `definitions/luauScriptTypes.gen.d.lua`.
- Set the FFlags `LuauRecursionLimit` and `LuauTarjanChildLimit` to something
  higher than the defaults (e.g. 5000 and 20000 respectively). This can be done
  through the `luau-lsp.fflags.override` preference.
  - This is necessary because the generated definition file is too large and
    complex for Luau to parse by default.

After doing this (and reloading the workspace), you should get type checking and
autocomplete for all Godot and `godot-luau-script` types.
