# Outstanding tasks/issues

## Recurring tasks

- Keeping up-to-date with GDExtension changes

## Other

- Full debugger support (pending
  [godot#95686](https://github.com/godotengine/godot/pull/95686))
- Tracy profiler integration
- Specialized LSP implementation
  ([luau-lsp](https://github.com/JohnnyMorganz/luau-lsp))
- Luau export plugin (to export bytecode to PCKs)
- Improved Callable access for custom classes
- Callable support for Luau functions
- Documentation support
- Cleanup after unloading map packs (e.g., dropping references to user scripts
  in the cache)
- Proper multithreading support (separate Luau thread pool managed by the
  runtime)
- Proper typed array and dictionary support (in types checking, real objects,
  and the analyzer)
- Better GDScript interop (e.g., by falling back to `Object::get`, `set`,
  `call`)
- Typechecking for `__iter`
- Better documentation of internal structures
- A way of viewing the entire Godot API and identifying necessary changes to
  permissions
- Allow consumers to override permissions
- Less verbose/confusing class definition syntax
- Investigate type-safe Object equality (without using IDs)
- A better way of declaring and propagating permissions
- Regression: Investigate crashes on first start (previous "restart required"
  hack invalidated by https://github.com/godotengine/godot/pull/93972)
