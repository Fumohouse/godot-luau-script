# "Core Scripts", VMs, and Permissions

These three concepts are essential to `godot-luau-script`'s sandboxing and
security.

## "Core Scripts"

Core scripts have special privileges such as being able to set their own
permissions. By default, no scripts are considered core scripts.

Scripts can be designated as core scripts through the
[`SandboxService`](./luau-script-api.md), typically using
[`init.lua`](./init-file.md).

## VMs

`godot-luau-script` currently runs 3 different Luau VMs for different use cases:

1. The loading VM, which is used to load essential script information (e.g. a
   list of methods and constant values).
2. The core VM, which is where all core scripts are executed.
3. The user VM, which is where all non-core scripts are executed.

This provides a certain level of isolation and security in addition to thread
sandboxing.

### Method calls

Within the same VM, any script can call any other script's methods, so long as
they are defined in the implementation table. Between VMs, scripts can only call
other scripts' methods if they are registered to Godot. This gives a rough
notion of "public" and "private".

## Permissions

As mentioned in the introduction, `godot-luau-script` was primarily created to
load untrusted scripts safely. This means that certain Godot APIs must be locked
behind special permissions, and that scripts will need to declare these
permissions to receive them.

The specific permission levels are listed [here](./class-registration.md). As
noted above, only a core script can declare its own permissions.

Certain APIs receive special permissions. The defaults are as follows:

1. `RefCounted` and `Node` classes are in `BASE`.
2. All editor-specific classes are in `BASE`.
3. All other classes are `INTERNAL`.

All deviations from the default permissions are recorded in
`bindgen/class_settings.toml`.

Permissions are set on the thread level and are inherited to child threads. If a
thread ever tries to execute a method or other code of a permission level it
does not have, it will cause an error.

Note that only methods that are registered to Godot are executed on their
script's thread. If any non-registered methods use protected APIs, classes with
Godot-registered methods that use them must declare the necessary permissions.
