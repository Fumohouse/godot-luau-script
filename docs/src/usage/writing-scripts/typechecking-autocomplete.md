# Typechecking and Autocomplete

Upon building, `godot-luau-script` will generate a single type definition file
for use by Luau's analyzer.

Currently, autocomplete and analysis in the Godot editor is not supported. As
such, the most viable setup is to use Visual Studio Code and the
[luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) extension.

## Setup

After installing VSCode and the extension above, you will need to do the
following setup:

- Set the preference `luau-lsp.require.mode` to `relativeToFile` to make
  `require` relative to the current script.
- Set the preference `luau-lsp.types.roblox` to `false` to disable Roblox types
  being loaded by default.
- Set the preference `luau-lsp.types.definitionFiles` to an array containing the
  path to `definitions/luauScriptTypes.gen.d.lua`.
- Set the FFlag `LuauRecursionLimit` to something higher than the default (e.g.
  5000). This can be done through the `luau-lsp.fflags.override` preference.
  - This is necessary because the generated definition file is too large for
    Luau to parse by default.

After doing this (and reloading the workspace), you should get type checking and
autocomplete for all Godot and `godot-luau-script` types.
