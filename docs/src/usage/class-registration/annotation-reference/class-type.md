# Class Type

The class type is the Luau type with the same name as the `local` that the
[`@class` annotation](./class-definition.md#class-globalclassname) refers to. It
is used to enable type checking for the class and declare the its associated
properties/signals.

Example:

```lua
--- @class
local MyClass = {}

-- This is the class type
export type MyClass = RefCounted & typeof(MyClass) & {
    -- Attach annotations in the "Property annnotations" section here.
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

## Property annotations

These annotations should be attached to the individual properties in your class
type.

### Property groups

These annotations are evaluated in order alongside `@property` and `@signal`.

#### `@propertyGroup [groupName]`

Denotes a property group visible in the Godot editor.

- `groupName`: The group name. If not supplied, the group for the next property
  is unset.

#### `@propertySubgroup [groupName]`

Denotes a subgroup of a property group visible in the Godot editor.

- `groupName`: The group name. If not supplied, the group for the next property
  is unset.

#### `@propertyCategory [groupName]`

Denotes a property category visible in the Godot editor.

- `groupName`: The group name. If not supplied, the category for the next
  property is unset.

### Properties

The following annotations apply only to properties.

#### `@property`

Indicates a property should be exposed to Godot, registered based on its type
annotation. All additional information necessary for the property is supplied
through other annotations.

#### `@default <defaultValue>`

Registers a property's default value, which will be automatically assigned when
a script instance is initialized if no setter or getter exists.

- `defaultValue`: The default value, in a format compatible with Godot's
  [`str_to_var`](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#class-globalscope-method-str-to-var).

#### `@set <method>` and `@get <method>`

Register a property's setter and getter, respectively.

- `method`: The name of the setter/getter. The method should be part of this
  class and registered to Godot.

#### `@range <min> <max> [step] [...'orGreater'|'orLess'|'hideSlider'|'radians'|'degrees'|'radiansAsDegrees'|'exp'|'suffix:'<suffix>]`

Valid only for `integer` and `number` properties. Defines the range (and
optional step) for a numeric value in the Godot editor.

Do not use decimals (including `.0`) for `integer` properties.

- `min`: The minimum value.
- `max`: The maximum value.
- `step`: The step value.
- `orGreater` and `orLess`: Allow the limit to apply only to the slider.
- `hideSlider`: Hide the slider.
- `radians` and `degrees`: Indicate the units of an angle value.
- `radiansAsDegrees`: Indicate the value is in radians but should be displayed
  as degrees in the editor.
- `exp`: Cause values to change exponentially.
- `suffix:<suffix>`: Provide a custom suffix for the value.

#### `@enum <...values>`

Valid only for `integer` and `string` properties. Defines a set of acceptable
values for a property in the Godot editor, either by index (for `integer`
properties) or name (for `string` properties).

- `values`: The enum values.

#### `@suggestion <...values>`

Valid only for `string` properties. Defines a set of suggested values for a
property in the Godot editor.

- `values`: The suggested values.

#### `@flags <...flagValues>`

Valid only for `integer` properties. Defines a set of bit flags that are easily
set in the Godot editor.

- `flagValues`: The names of the flag values.

#### `@file ['global'] [...extensions]`

Valid only for `string` properties. Indicates to the Godot editor that a string
should be a file path.

- `global`: If supplied, the path is not constrained to the current Godot
  project.
- `extensions`: Desired extensions in the format `*.ext`, for example `*.png`
  and `*.jpeg`.

#### `@dir ['global']`

Valid only for `string` properties. Indicates to the Godot editor that a string
should be a directory path.

- `global`: If supplied, the path is not constrained to the current Godot
  project.

#### `@multiline`

Valid only for `string` properties. Indicates to the Godot editor that a string
should have a multiline editor.

#### `@placeholderText <text>`

Valid only for `string` properties. Indicates to the Godot editor that a certain
string should be displayed in the property's text field if it is empty.

- `text`: The placeholder text.

#### `@flags{2D,3D}{Render,Physics,Navigation}Layers`

Valid only for `integer` properties. Indicate to the Godot editor that one of
the various layer editors should be shown in place of the default number editor.

#### `@expEasing ['attenuation'|'positiveOnly']`

Valid only for `number` properties. Indicates to the Godot editor that a easing
curve editor should be shown in place of the default number editor.

- `attenuation`: Flips the curve horizontally.
- `positiveOnly`: Limit values to those greater than or equal to zero.

#### `@noAlpha`

Valid only for `Color` properties. Indicates to the Godot editor that the color
should not have an editable alpha channel.

### `@signal`

Indicates a property with the `Signal` or
[`SignalWithArgs<F>`](../type-conversion.md#signalwithargsf) type should be
registered as a signal.
