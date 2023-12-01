# Tests

`godot-luau-script` contains tests written using the
[Catch2](https://github.com/catchorg/Catch2) framework. They can be built using
the `tests=yes` flag when running `scons`.

Tests can be run by running Godot in the `test_project` directory with the
`--luau-tests` flag. Then, Catch2 flags can be given after supplying a `--`
argument.

## Note for Windows users

`godot-luau-script` uses a symbolic link to link `bin/` to `test_project/bin/`.
These do not work on Windows. If you want to run tests, you will need to copy
the folder (including built binaries) yourself.
