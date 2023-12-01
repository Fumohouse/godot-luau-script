# Assigns

The following annotations should be attached to assignments to fields on the
table denoted by `@class`. For example:

```lua
-- <truncated>

--- @registerConstant
MyClass.TEST_CONSTANT = 5
```

## `@registerConstant`

Valid only for assignments with `Variant` values. Registers a constant value
with Godot.
