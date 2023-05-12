# Debugging

Assuming you built `godot-luau-script` with the `debug_symbols=yes` flag,
you will be able to debug the project by running Godot with a debugger attached.

In case you run into issues which the debugger cannot trace, they may be caused by Godot engine code.
You can debug engine code by cloning Godot and building it with debug symbols as well.
Be sure to `git checkout` the commit matching the current supported version of Godot before building.
