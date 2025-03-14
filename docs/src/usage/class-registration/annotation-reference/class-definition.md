# Class Definition

These annotations should be attached to the `local` defining the original class
table (in [this example](../defining-a-class.md), `MyClass`).

## `@class [globalClassName]`

Required to define a class.

- `globalClassName`: If supplied, the class will be globally visible to Godot.

Note: this annotation reserves the Luau type with the same name as the `local`
it is applied to for use as the [class type](./class-type.md).

## `@extends <base>`

Defines a script's base class.

- `base`: Either a valid Godot class name, or the name of a local containing a
  directly `require`d script.

`require` example:

```lua
-- Your require call must directly contain a path.
-- It must appear before the @extends annotation.
local BaseClass = require("BaseClass.lua")

--- @class
--- @extends BaseClass
local MyClass = {}

-- <truncated>
```

## `@tool`

Indicates the script is instantiable/runnable in the editor.

## `@permissions <...permissionsFlags>`

Declares the script's permissions (see ["Core Scripts", VMs, and
Permissions](../../core-scripts-vms-permissions.md)).

- `permissionsFlags`: Valid permissions flags.

The following permissions flags are accepted:

- `BASE`: Used for functionality that is available to all scripts by default.
- `INTERNAL`: Used for functionality that is not part of any special permission
  level or `BASE`.
- `OS`: Used for the `OS` singleton.
- `FILE`: Used for any filesystem functionality.
- `HTTP`: Used for any functionality allowing HTTP listen/requests.

## `@iconPath <path>`

Defines a script's icon in the editor.

- `path`: The path to the icon. Must be absolute and point to an SVG image.
