# "Core Scripts", VMs, and Permissions

These three concepts are essential to `godot-luau-script`'s sandboxing and
security.

## "Core Scripts"

A core script is any script which is available in the filesystem whenever
`godot-luau-script` is loaded. This means that any scripts part of a .pck file
that was loaded after `godot-luau-script` should be declared non-core.

## VMs

`godot-luau-script` currently runs 3 different Luau VMs for different use cases:

1. The loading VM, which is used to load essential script information
   (essentially, everything outside of its implementation table).
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
noted there, only a core script can declare its own permissions.

Certain APIs receive special perimssions, with `INTERNAL` being the default.
Permissions are set on the thread level, and are inherited to child threads.
If a thread ever tries to execute a method or other code of a permission level
it does not have, it will cause an error.

A comprehensive list of the permissions assigned to every Object class and
method can be seen at `bindgen/class_settings.toml`.
