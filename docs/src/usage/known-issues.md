# Known Issues

`godot-luau-script` and Godot's GDExtension script language interface are both
relatively unstable. As such, you may experience issues when using them
together.

## Error: `_gdvirtual__get_language_call: Required virtual method ScriptExtension::_get_language must be overridden before calling.`

This is likely an upstream issue with the initialization of GDExtension classes.
Scripts should run normally despite this error.

The issue is tracked [here](https://github.com/godotengine/godot/issues/80275).
