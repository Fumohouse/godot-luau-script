# Virtual Methods

`godot-luau-script` supports the overriding of virtual methods.

## Defined in an `Object` class

If a virtual method such as `_process` or `_ready` is defined on an `Object`-type class,
you can override it by defining it in your implementation table and registering it to Godot.

## Built in to `godot-luau-script`

This project comes with a number of built-in virtual methods, which can be overridden just by defining them on your implementation table.
They do not need to be registered to Godot.

### `function _Init(self): ()`

`_Init` is called as soon as an `Object` with your script is instantiated.

At this time, you can initialize any values on the object or the underlying table.
Note that the virtual `_Ready` on `Node` may be equally or more appropriate in many cases.

### `function _Notification(self, what: number): ()`

`_Notification` is called whenever your object receives a notification.
Refer to Godot documentation to see what notifications you may receive.

### `function _ToString(self): string`

`_ToString` is called whenever Godot wants to stringify your object, for example when printing your object or calling `tostring`.
It should return the string representation of your object.

### Custom property methods

These methods should be used in combination to define custom properties for your object, typically so they can show up in the editor.
Keep in mind you must set `:Tool(true)` on your class for these methods to run in the editor.

#### `function _GetPropertyList(self): {GDProperty}`

This function should return a list of additional properties to be registered to Godot.

#### `function _Set(self, name: string, value: Variant): boolean`

This function should handle setting a value for a custom property.
It should return `true` when it set the value successfully, and `false` when it did not (or when the property doesn't exist).

#### `function _Get(self, name: string): Variant`

This function should handle getting a value for a custom property.
It should return the value of the property or `nil` if the property doesn't exist.

#### `function _PropertyCanRevert(self, name: string): boolean`

This function should return `true` if the given property has a default value which the editor should be able to revert to.

#### `function _PropertyGetRevert(self, name: string): Variant`

This function should be overridden along with `_PropertyCanRevert`.
It should return the default value of the given property, or `nil` if it doesn't exist.
