# Building and Installation

## Download

Prebuilt binaries for each tagged release can be found on the [releases
page](https://git.seki.pw/Fumohouse/godot-luau-script/releases).

## Installation

All necessary GDExtension files are provided in `bin/`. It should suffice to
copy the entire folder into your Godot project. After reloading the project, the
extension should be loaded and you should be able to create a Luau script
through the script creation dialog.

## Building yourself

`godot-luau-script` uses [SCons](https://scons.org/) as its build system.

### Cloning

The three dependencies this project has, godot-cpp, Luau, and Catch2, are added
as submodules in `extern/`. As such, make sure to clone submodules when cloning
this project:

- `git clone https://github.com/Fumohouse/godot-luau-script
  --recurse-submodules`, or
- `git submodule update --recursive` if the repository is already cloned.

### Supported platforms

`godot-luau-script` supports compilation using recent versions of Clang, GCC,
MinGW, and MSVC, and building for Windows, macOS, and Linux. LLVM (`clang++`,
`clangd`, `clang-format`) utilities are preferred where possible.

### Building

Ensure that a supported toolchain is installed. You will also need to install
[Python 3](https://www.python.org/) and run `python -m pip install scons` to
install SCons. If on Linux, SCons is also available through some package
repositories.

If you are running a Python version older than 3.11, you will need to
`python -m pip install tomli` for TOML support.

Build the project by running `scons` at the project root. The following flags
are supported:

- `tests=yes`: Build with [Catch2 tests](../development/tests.md) included.
- `iwyu=yes`: Run [`include-what-you-use`](https://github.com/include-what-you-use/include-what-you-use)
  instead of the compiler to find potential `#include` mistakes.
- `generate_luau_bindings=yes`: Force regeneration of any auto-generated files
  (see `bindgen/`).

Additionally, you may want to use the following flags from godot-cpp:

- `target=editor`: Build for the editor.
- `target=template_release`: Build for release templates.
- `use_llvm=yes`: Force usage of LLVM for compilation (over GCC).
- `debug_symbols=yes`: Build the project with debug symbols.
- Run `scons compiledb target=editor tests=yes use_llvm=yes` to generate a
  compilation commands database, typically for use with a language server like
  `clangd`.

After building, the output will be present in the `bin/` folder.

## Build containers

`podman` build containers are provided in `build_containers/` for reproducible
build environments targeting Linux, Windows, and macOS.

### Usage

Scripts should not be run outside the directory they are in.

1. Ensure `podman` is installed
2. Build images with `build_images.sh`
3. Build the project with `build_gls.sh` - use arguments `linux`, `windows`, and
   `macos` to select which platform(s) to build

### Preparing `osxcross`

Go to
[tpoechtrager/osxcross](https://github.com/tpoechtrager/osxcross/tree/master)
and follow an appropriate procedure for downloading Xcode and packaging the SDK
on your system.

Place the output SDK (expected name: `MacOSX13.3.sdk.tar.xz`) in
`build_containers/files/`.

Build was tested on Xcode 14.3.1 (SDK: macOS 13.3).
