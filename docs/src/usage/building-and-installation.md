# Building and Installation

Prebuilt binaries can be found on the [releases page](https://github.com/Fumohouse/godot-luau-script/releases).

If you would like to build this project yourself, instructions are provided
below.

## Building

`godot-luau-script` uses [SConstruct](https://scons.org/) to build.

### Cloning

The three dependencies this project has, godot-cpp, Luau, and Catch2, are added
as submodules in `extern/`. As such, make sure to clone submodules when cloning
this project:

- `git clone https://github.com/Fumohouse/godot-luau-script --recurse-submodules`, or
- `git submodule update --recursive` if the repository is already cloned.

#### Windows

`godot-luau-script` uses a symlink to link `bin/` to `test_project/bin/`. These
symbolic links are not supported on Windows. If you want to run tests, you will
need to copy the folder (including built binaries) yourself.

### Supported platforms

`godot-luau-script` officially supports compilation using recent versions of
Clang, GCC, MinGW, and MSVC, and building for Windows, macOS, and Linux. LLVM
(`clang++`, `clangd`, `clang-format`) utilities are preferred where possible.

### Building

Ensure that a supported toolchain is installed. You will also need to install
[Python 3](https://www.python.org/) and run `python -m pip install scons` to
install SConstruct (or, if on Linux, SConstruct is available through some
package repositories).

If you are running a Python version older than 3.11, you will need to
`python -m pip install tomli` for TOML support.

Build the project by running `scons` at the project root. The following flags
are supported:

- `tests=yes`: Build with [Catch2 tests](../development/tests.md) included.
- `iwyu=yes`: Run [`include-what-you-use`](https://github.com/include-what-you-use/include-what-you-use)
  instead of the compiler to find potential `#include` mistakes.
- `generate_luau_bindings=yes`: Force regeneration of any auto-generated files
  (see `bindgen/`).
- `cdb`: Force generation of the `compile_commands.json`, which is used by
  language servers like `clangd` for analysis.

Additionally, you may want to use the following flags from godot-cpp:

- `target=editor`: Build for the editor.
- `target=template_release`: Build for release templates.
- `use_llvm=yes`: Force usage of LLVM for compilation (over GCC).
- `debug_symbols=yes`: Build the project with debug symbols.

After building, output will be present in the `bin/` folder.

## Installation

Installing `godot-luau-script` is relatively easy. All necessary GDExtension
files are provided in `bin/`; it should suffice to copy the entire folder into
your Godot project. After reloading the project, the extension should be loaded
and you should be able to create a Luau script through the script creation
dialog.

## Build containers

`podman` build containers are provided in `build_containers/` for reproducible
build environments on Linux, Windows, and macOS.

### Usage

Scripts should not be run outside the directory they are in.

1. Ensure `podman` is installed
2. Build images with `build_images.sh`
3. Build the project with `build_gls.sh` - use arguments `linux`, `windows`, and
   `macos` to select which platform(s) to build

### Preparing `osxcross`

See [tpoechtrager/osxcross](https://github.com/tpoechtrager/osxcross/tree/master).

Place the SDK (expected name: `MacOSX13.3.sdk.tar.xz`) in .tar.xz format into
`build_containers/files/`.

Some workarounds may be required for the scripts to function on recent Xcode
versions (see [this issue comment](https://github.com/tpoechtrager/osxcross/issues/383#issuecomment-1580487598)).

Build was tested on Xcode 14.3.1 (SDK: macOS 13.3).
