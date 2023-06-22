# Class Registration

Class registration in `godot-luau-script` is done through annotations,
essentially an extension of Luau's syntax.

## Annotations

Annotation comments do not follow any code and begin with three dashes in a row.
They must be continuous, i.e. there cannot be a completely blank line between
comment lines.

Annotations begin with an `@` followed by a `camelCase` name.  Where there are
multiple arguments, they are separated by spaces.

An optional text comment can precede the annotation comments. This comment is
currently parsed but not used for anything.

```lua
--- This is a comment. Documentation can go here.
---
--- The empty comment above indicates a line break.
--- @annotation1 arg1 arg2 arg3
--- @annotation2 arg1 arg2 arg3

--- This is a comment.

--- This is a different comment since the above line is empty.

print("hello world!") --- This is not an annotation comment.

--[[
    This is not an annotation comment.
]]
```

## Defining a class

`godot-luau-script` classes are simply a special table created using `gdclass`.

Both the original table and the output of `gdclass` are expected to be stored
in locals, typically at the start of the file.

Annotations used for class registration, such as `@class`, should be placed
on the original table local and not the local storing the output of `gdclass`.

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

## Annotation reference

Key:

- `[]` denotes an optional argument
- `<>` denotes a required argument
- `...` means an argument can be supplied multiple times
- `|` means 'or'
- `''` denotes a literal argument supplied to the annotation without quotes
- `{}` denotes multiple choices at any given location

### Class definition

These annotations should be attached to the local defining the original class
table (in the above example, `MyClass`).

#### `@class [global class name]`

The `@class` annotation is required to define a class. If the class name is
supplied, the class will be globally visible to Godot.

#### `@extends <Godot class name|imported script class>`

The `@extends` annotation defines a script's base class. It accepts either a
valid Godot class name, or the name of a local containing a directly `require`d
script, for example:

```lua
-- Your require call must directly contain a path.
-- It must appear before the @extends annotation.
local BaseClass = require("BaseClass.lua")

--- @class
--- @extends BaseClass
local MyClass = {}

-- <truncated>
```

#### `@tool`

The `@tool` annotation indicates the script is instantiable/runnable in the
editor.

#### `@permissions <...permissions>`

The `@permissions` annotation declares the script's permissions (see
["Core Scripts", VMs, and Permissions](./core-scripts-vms-permissions.md)).

The following permissions flags are accepted:

- `BASE`: Used for functionality that is available to all scripts by default.
- `INTERNAL`: Used for functionality that is not part of any special permission
  level or `BASE`.
- `OS`: Used for the `OS` singleton.
- `FILE`: Used for any filesystem functionality.
- `HTTP`: Used for any functionality allowing HTTP listen/requests.

#### `@iconPath <absolute svg path>`

The `@iconPath` annotation defines a script's icon in the editor. The path must
be absolute and point to an svg image.

### `@classType <class local>`

The `@classType` annotation links a class to its type definition, which is used
for registering properties and signals. `<class local>` refers to the local
to which your `@class` annotation is attached. For example:

```lua
--- @class
local MyClass = {}

--- @classType MyClass
export type MyClass = RefCounted & typeof(MyClass) & {
    -- Attach annotations in the "Class type" section here.
    tableField1: number,
    tableField2: string,
} & {
    -- It's okay to intersect multiple table types.
    -- Annotations will still be parsed.
    tableField3: Vector2,
}

-- <truncated>
```

Only plain table types and intersections (`&`, not `|`) are supported.

### Class type

The following annotations should be attached to the various table properties in
the class type denoted by `@classType`.

#### Property groups

These annotations are evaluated in order alongside `@property` and `@signal`.

##### `@propertyGroup [group name]`

The `@propertyGroup` annotation denotes a property group visible in the Godot
editor.

If the group name is not supplied, the group for the next property is unset.

##### `@propertySubgroup [group name]`

The `@propertySubgroup` annotation denotes a subgroup of a property group
visible in the Godot editor.

If the group name is not supplied, the group for the next property is unset.

##### `@propertyCategory [group name]`

The `@propertyCategory` annotation denotes a property category visible in the
Godot editor.

If the group name is not supplied, the category for the next property is unset.

#### Properties

The following annotations apply only to properties.

##### `@property`

The `@property` annotation indicates a property should be exposed to Godot,
registered based on the type annotation. All information necessary for the
property is supplied through syntax and other annotations.

##### `@default <default value>`

The `@default` annotation registsers a property's default value which will be
automatically assigned when a script instance is initialized if no setter or
getter exists.

The default value should be compatible with Godot's [`str_to_var`](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#class-globalscope-method-str-to-var).

##### `@set <method>` and `@get <method>`

The `@set` and `@get` annotations register a property's setter and getter,
respectively. The supplied method name should be a method on this class
registered to Godot.

##### `@range <min> <max> [step]`

The `@range` annotation, valid only for `integer` and `number` properties,
defines the range (and optional step) for a numeric value in the Godot editor.

Do not use decimals (including `.0`) for `integer` properties.

##### `@enum <...enum options>`

The `@enum` annotation, valid only for `integer` and `string` properties,
defines a set of acceptable values for a property in the Godot editor,
either by index (for `integer` properties) or name (for `string` properties).

##### `@suggestion <...options>`

The `@suggestion` annotation, valid only for `string` properties, defines a set
of suggested values for a property in the Godot editor.

##### `@flags <...options>`

The `@flags` annotation, valid only for `integer` properties, defines a set of
bit flags which are easily set in the Godot editor.

##### `@file [...'global'|extension]`

The `@file` annotation, valid only for `string` properties, indicates to the
Godot editor that a string should be a file path. If the `global` argument is
supplied, the path is not constrained to the current Godot project. Extensions
should be in the format `*.ext`, for example `*.png` and `*.jpeg`.

##### `@dir ['global']`

The `@dir` annotation, valid only for `string` properties, indicates to the
Godot editor that a string should be a directory path. If the `global` argument
is suppllied, the path is not constrained to the current Godot project.

##### `@multiline`

The `@multiline` annotation, valid only for `string` properties, indicates to
the Godot editor that a string should have a multiline editor.

##### `@placeholderText <placeholder text>`

The `@placeholderText` annotation, valid only for `string` properties, indicates
to the Godot editor that a certain string should be displayed in the property's
text field if it is empty.

##### `@flags{2D,3D}{Render,Physics,Navigation}Layers`

The various `@flags...Layers` annotations, valid only for `integer` properties,
indicate to the Godot editor that the various layer editors should be shown
in place of the default number editor.

##### `@expEasing ['attenuation'|'positiveOnly']`

The `@expEasing` annotation, only valid for `number` properties, indicates to
the Godot editor that a easing curve editor should be shown in place of the
default number editor.

##### `@noAlpha`

The `@noAlpha` annotation, only valid for `Color` properties, indicates to the
Godot editor that the color should not have an editable alpha channel.

#### `@signal`

The `@signal` annotation indicates a property with the `Signal` or
[`SignalWithArgs<F>`](#signalwithargsf) type should be registered as a signal.

### Methods

The following annotations should be attached to methods defined directly on
the table indicated by `@class`. For example:

```lua
-- <truncated>

--- @registerMethod
function MyClass.TestMethod()
end
```

The following is not currently valid:

```lua
-- <truncated>

-- Not recognized
--- @registerMethod
MyClass.TestMethod = function()
end
```

#### `@registerMethod`

The `@registerMethod` annotation indicates a method should be accessible to
Godot, registered based on its type annotations.

#### `@param <name> [comment]`

The `@param` annotation indicates a parameter's usage. It is not currently used
by `godot-luau-script`.

#### `@defaultArgs <array>`

The `@defaultArgs` annotation indicates a method's default arguments when called
from Godot. The rightmost value corresponds to the rightmost value.

The array should be compatible with Godot's [`str_to_var`](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#class-globalscope-method-str-to-var).

#### `@return [comment]`

The `@return` annotation documents a return value. It is not currently used by
`godot-luau-script`.

#### `@flags [...flags]`

The `@flags` annotation overrides a method's flags.

See the [Godot documentation](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#enum-globalscope-methodflags)
for a list of flags and their meanings (omit `METHOD_FLAG_`).

#### `@rpc ['anyPeer'|'authority'] ['unreliable'|'unreliableOrdered'|'reliable'] ['callLocal'] [channel]`

The `@rpc` annotation registers a remote procedure call with Godot. Its various
parameters affect its functionality.

See the [this Godot blog post](https://godotengine.org/article/multiplayer-changes-godot-4-0-report-2/) for details.

### Assigns

The following annotations should be attached to assignments to fields on the
table denoted by `@class`. For example:

```lua
-- <truncated>

--- @registerConstant
MyClass.TEST_CONSTANT = 5
```

#### `@registerConstant`

The `@registerConstant` annotation, valid for assignments with `Variant` values,
registers a constant value with Godot.

## Luau to Godot type conversion

`godot-luau-script` does its best to determine the proper Godot type from Luau
type annotations. However, it may fail under some circumstances. Here are a few
things to note:

- Aliasing Godot types (e.g. `type D = Dictionary`) is not currently supported.
- Using the `?` syntax (e.g. `number?`) will tell Godot that any `Variant` type
  is acceptable. This is because the only Godot type that accepts a value and
  `null` is `Variant`. As such, it's recommended to implement your methods such
  that this is not necessary.

## Special types

### `TypedArray<T>`

When used for methods and properties, `TypedArray<T>` will supply Godot with
the correct type for `Array` elements.

### `NodePathConstrained<T...>`

When used for methods and properties, `NodePathConstrained<T...>` will tell
Godot to constrain valid `Node` types to the given types. Only supply types
which extend `Node`.

### `SignalWithArgs<F>`

When used for signals, `SignalWithArgs<F>` will tell Godot the parameters of the
registered signal. The supplied type `F` should be a void (`-> ()`) function
type with no generics and any number of arguments.

Argument names are optional. If not supplied, the default is `argN` where `N`
is the index of the argument starting from 1.
