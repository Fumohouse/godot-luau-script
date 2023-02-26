# Accessing Godot APIs

The majority of builtin and `Object` classes are bound from Godot to Luau.
A selection of utility functions from Godot are also available.

To find out what APIs are exposed, you can refer to the `definitions/luauScriptTypes.gen.d.lua` file after building (beware: this file is large!)
or rely on autocomplete to tell you (after setting that up).

For the most part, you can refer to [Godot's documentation](https://docs.godotengine.org/en/latest/) to discover the API.
After learning the rules below, this should become relatively intuitive.

## Renaming rules

Most members are renamed from their Godot names to the following convention:

| Type               | Godot Case                | Luau Case                              |
| ------------------ | ------------------------- | -------------------------------------- |
| Classes and enums  | `PascalCase`              | *unchanged*                            |
| Utility functions  | `snake_case`              | *unchanged with few exceptions*        |
| Enum values        | `PREFIX_UPPER_SNAKE_CASE` | `UPPER_SNAKE_CASE` *with exceptions* * |
| Constants          | `UPPER_SNAKE_CASE`        | *unchanged*                            |
| Methods            | `snake_case`              | `PascalCase`                           |
| Properties/signals | `snake_case`              | `camelCase`                            |

*: e.g. for the `PropertyUsage` enum, the prefix on all values is `PROPERTY_USAGE_`; this is removed.
Additionally, if an enum name begins with a number after renaming (e.g. `KEY_9` with prefix `KEY_` -> `9`), an `N` will be prepended to the name -> `N9`.

## Accessible APIs

### Globals

| Type             | Godot Access   | Luau Access            | GDScript Example       | Luau Example        |
| ---------------- | -------------- | ---------------------- | ---------------------- | ------------------- |
| Global enum      | `@GlobalScope` | `Enum.` namespace      | `MARGIN_LEFT`          | `Enum.Margin.LEFT`  |
| Global constant  | `@GlobalScope` | `Constants.` namespace | *N/A*                  | *N/A*               |
| Utility function | `@GlobalScope` | `_G`                   | `lerpf(0.0, 1.0, 0.5)` | `lerp(0, 1, 0.5)` * |

*: This is one of the renamed functions mentioned above.

### Variant and Object classes

| Type                          | Godot Access                  | Luau Access                            | GDScript Example        | Luau Example               |
| ----------------------------- | ----------------------------- | -------------------------------------- | ----------------------- | -------------------------- |
| Variant constructors          | `<ClassName>`                 | `<ClassName>.new`                      | `Vector3(0, 1, 0)`      | `Vector3.new(0, 1, 0)`     |
| Object constructors           | `<ClassName>.new`             | *unchanged*                            | `AESContext.new()`      | *unchanged*                |
| Object singleton              | `<ClassName>`                 | `<ClassName>.GetSingleton()`           |
| Static methods                | `<ClassName>.<Method>`        | *unchanged*                            | `Vector2.from_angle(x)` | `Vector2.FromAngle(x)`     |
| Instance methods static call  | *N/A*                         | `<ClassName>.<Method>`                 | *N/A*                   | `Vector2.Dot(v1, v2)`      |
| Instance methods              | `<Instance>.<Method>`         | `<Instance>:<Method>`                  | `v1.dot(v2)`            | `v1:Dot(v2)`               |
| Member/property/signal access | `<Instance>.<Property>`       | *unchanged* *                          | `vector.x`              | *unchanged*                |
| Keyed/indexed set             | `<Instance>[<Key>] = <Value>` | `<Instance>:Set(<Key>, <Value>)`       | `dictionary["key"] = 1` | `dictionary:Set("key", 1)` |
| Keyed/indexed get             | `<Instance>[<Key>]`           | `<Instance>:Get(<Key>)`                | `dictionary["key"]`     | `dictionary:Get("key")`    |
| Array length                  | `<Array>.size()`              | `<Array>:Size()` OR `#<Array>`         | `array.size()`          | `array:Size()` OR `#array` |
| Array iteration               | `for item in <Array>:`        | `for index, item in <Array> do` **     |
| Dictionary iteration          | `for key in <Dictionary>:`    | `for key, value in <Dictionary> do` ** |
| Variant type operators        | `<A> <Op> <B>`/`<Unary><A>`   | *unchanged* \*\*\*                     | `v1 == v2`              | *unchanged*                |
| Variant/Object to string      | `str(<Instance>)`             | `tostring(<Instance>)`                 |

*: Variant type properties (e.g. `Vector2.x`) **cannot be set** because Luau does not support copy on assign (as C++ and GDScript do). You must construct a new object instead. \
**: Iterators do not support modification during iteration. Doing so may cause errors or for items to be skipped. \
***: Some Godot operators are not supported as they do not exist in Luau. Also, `==` comparison is not allowed between two different types in Luau, so these operators do not work.

### Odd exceptions

- For security (permissions) reasons, the only supported `Callable` constructor is `Callable.new(object: Object, methodName: string | StringName)`.
- `String`, `StringName`, and `NodePath` are not bound to Luau as Luau's builtin `string` suffices in the vast majority of cases. `StringName` and `NodePath` can be constructed manually if needed (e.g. if a `String` type would be inferred over the other two types) by using the `SN` or `NP` global functions respectively.
  - The intention with these constructors is to use them like a prefix you would find in other languages, e.g. `SN"testStringName"`. This is valid because of Lua's lenient parsing rules.
