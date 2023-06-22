# Modules

`godot-luau-script` supports non-class module scripts and the `require`
function.

## `require`

The `require` function accepts a path relative to the current script, without
the `.lua` extension. It will load the file at the given path and return
whatever the file returned.

Requires are cached within the same Lua VM, and are reloaded when necessary (if
the script changes).

Please note that cyclic dependencies are not supported and may cause odd
behavior in the editor if encountered.

## Module scripts

`require` can require a script (class) file, in which case it will return the
output of `gdclass` for the class.

However, you may want to create your own Lua types which don't need to be
accessible to Godot. To do this in a dedicated file, create a file with the
extension `.mod.lua` which returns a function or a table.

Then, you will be able to require this file (being sure to include the `.mod`
but not the `.lua` in the require path).
