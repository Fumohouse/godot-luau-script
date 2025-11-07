# Outstanding tasks/issues

## Bugs

- The `GDThreadStack` system does not consider nested calls, causing consistent
  breakage for `Signal` and other functionality.
- Crashes at first start

## Tasks

- Full debugger support, blocked on
  [godot#95686](https://github.com/godotengine/godot/pull/95686).
- [Tracy profiler](https://github.com/wolfpld/tracy) integration
- Specialized LSP implementation based on
  [luau-lsp#505](https://github.com/JohnnyMorganz/luau-lsp/pull/505).
- Luau export plugin for outputting bytecode and metadata on project export
- Improved `Callable` access for custom classes (rather than `Callable.new`)
  - Could attempt to coerce functions into callables
- `CallableCustom` for Luau functions
- Documentation support for Luau classes
- Cleanup after unloading user code
- Proper multithreading support (rather than just coroutines)
- Proper typed array and dictionary support, in type checking, real objects, and
  the analyzer
- Restore keyed access to arrays and dictionaries (must use new type solver)
- Better interoperability between GDScript and Luau
- Typechecking for `__iter`, blocked on the [recursive type
  restriction](https://rfcs.luau.org/relax-recursive-type-restriction.html)
- Better documentation of internal functionality
- Permissions audit
- Allow consumers of the extension to override permissions
- Less verbose/confusing class syntax
  - Annotations should stay for documentation, but all other functionality
    should be declared in another way
  - Could use a [type
    function](https://rfcs.luau.org/user-defined-type-functions.html) to convert
    a table (used for class declaration) into a class type
- Make type annotations optional
- Consider deducing types using type analysis rather than the AST
- Type-safe Object equality (without using object IDs)
- Better way of declaring and propagating permissions (rather than declaring at
  class level and not working for module scripts)
- Improving engine call overhead
- Consider switching to the native `vector` library (as Roblox has)
