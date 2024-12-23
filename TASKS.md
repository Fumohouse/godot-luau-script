# Outstanding tasks/issues

- Full debugger support (pending [godot#95686](https://github.com/godotengine/godot/pull/95686))
- Tracy profiler integration
- Specialized LSP implementation ([luau-lsp](https://github.com/JohnnyMorganz/luau-lsp))
- Luau export plugin (to export bytecode to PCKs)
- Callable support for Luau functions
- Documentation support
- Cleanup after unloading map packs (e.g., dropping references to user scripts in
  the cache)
- Proper multithreading support (separate Luau thread pool managed by the
  runtime)
- Proper typed array and dictionary support (in types checking, real objects,
  and the analyzer)
- Better GDScript interop (e.g., by falling back to `Object::get`, `set`,
  `call`)
- Typechecking for `__iter`
- Keeping up-to-date with GDExtension changes:
  - https://github.com/godotengine/godot/pull/98914
- Improving the storage of class settings
