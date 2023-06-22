# Tests

`godot-luau-script` contains tests written using the
[Catch2](https://github.com/catchorg/Catch2) framework. They can be built using
the `tests=yes` flag when running `scons`.

Tests can be run by running Godot in the `test_project` directory with the
`--luau-tests` flag. Then, Catch2 flags can be given after supplying a `--`
argument.
