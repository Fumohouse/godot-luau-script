# Methods

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

## `@registerMethod`

Indicates a method should be accessible to Godot, registered based on its type
annotations.

If there are no type annotations, `Variant` is assumed for all arguments,
and the return value is `Variant` if the method returns anything.

## `@param <name> [comment]`

Indicates a parameter's usage. Not currently used by `godot-luau-script`.

- `name`: The name of the parameter.
- `comment`: Comment describing the parameter.

## `@defaultArgs <array>`

Indicates a method's default arguments when called from Godot.

- `array`: The default values, in the format of an array compatible with Godot's
  [`str_to_var`](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#class-globalscope-method-str-to-var).
  The rightmost value corresponds to the rightmost argument.

## `@return [comment]`

The `@return` annotation documents a return value. It is not currently used by
`godot-luau-script`.

- `comment`: Comment describing the return value.

## `@flags [...methodFlags]`

Overrides a method's flags.

See the [Godot documentation](https://docs.godotengine.org/en/stable/classes/class_@globalscope.html#enum-globalscope-methodflags)
for a list of flags and their meanings (omit `METHOD_FLAG_`).

- `methodFlags`: The flag values.

## `@rpc ['anyPeer'|'authority'] ['unreliable'|'unreliableOrdered'|'reliable'] ['callLocal'] [channel]`

Registers a remote procedure call with Godot.

See the [this Godot blog post](https://godotengine.org/article/multiplayer-changes-godot-4-0-report-2/) for details.

- `anyPeer`: Allow the RPC to be called by any peer.
- `authority`: Only allow the RPC to be called by the multiplayer authority.
- `unreliable`: Transfer data unreliably without resending.
- `unreliableOrdered`: Transfer data unreliably without resending, but in the
  correct order. Any older packets are dropped. This flag should not be used for
  different types of messages on the same channel.
- `reliable`: Transfer data reliably; resend packets if they don't arrive.
- `callLocal`: Indicate this method must also be called locally when the RPC is
  sent.
- `channel`: Indicate the channel this RPC should use. Each channel handles
  ordering of packets separately.
