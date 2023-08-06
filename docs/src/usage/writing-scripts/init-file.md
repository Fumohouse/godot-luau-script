# init.lua

If `res://init.lua` exists, it is run with on the [core](./core-scripts-vms-permissions.md)
VM with all permissions enabled. Certain functionality, such as `require` and
`gdclass`, are disabled as `init.lua` is not considered a script file.

This file can be used to configure specific things which can/should be
initialized as soon as `LuauLanguage` is initialized, such as core script
detection and sandboxing settings.
