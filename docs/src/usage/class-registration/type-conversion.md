# Type Conversion

`godot-luau-script` does its best to determine the proper Godot type from Luau
type annotations. Here are a few things to note about this process:

## Nullable (`?` or `T | nil`) types

When annotating a method argument with a nullable type, the argument will be
registered to Godot as non-nullable (i.e. `number?` -> `FLOAT`). If you want to
have the argument be optional on the Godot side, [register default arguments](./annotation-reference/methods.md#defaultargs-array).

When annotating anything else (properties, return types, etc.), using a nullable
type will cause the type to be registered with Godot as `Variant` unless the
type extends `Object`. This is because the only Godot type that accepts both
a value and `null` is `Variant`. If you encounter this, it's recommended to
reimplement your logic such that using a nullable is not necessary.

## `TypedArray<T>`

When used for methods and properties, `TypedArray<T>` will supply Godot with
the correct type for `Array` elements.

## `NodePathConstrained<T...>`

When used for methods and properties, `NodePathConstrained<T...>` will tell
Godot to constrain valid `Node` types to the given types. Only supply types
which extend `Node`.

## `SignalWithArgs<F>`

When used for signals, `SignalWithArgs<F>` will tell Godot the parameters of the
registered signal. The supplied type `F` should be a void (`-> ()`) function
type with no generics and any number of arguments.

Argument names are optional. If not supplied, the default is `argN` where `N`
is the index of the argument starting from 1.

## Other potential issues

- Aliasing Godot types (e.g. `type D = Dictionary`) is not currently supported.
