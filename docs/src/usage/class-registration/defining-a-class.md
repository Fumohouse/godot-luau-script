# Defining a Class

`godot-luau-script` classes are essentially a special table created using
`gdclass`.

Both the original table and the output of `gdclass` are expected to be stored in
locals, typically at the start of the file.

Annotations used for class registration, such as `@class`, should be placed on
the original table local and not the local storing the output of `gdclass`.

The output of `gdclass` should be returned at the end of the file.

Valid example:

```lua
--- @class
local MyClass = {}
local MyClassC = gdclass(MyClass)

return MyClassC
```

Invalid example:

```lua
--- @class
MyClass = {} -- INVALID: The class parser ignores globals

return gdclass(MyClass) -- INVALID: The class parser will not trace this return
```
