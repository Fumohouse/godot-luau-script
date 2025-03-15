{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell.override { stdenv = pkgs.clang19Stdenv; } {
  nativeBuildInputs = with pkgs; [
    scons
    clang-tools

    linuxPackages_latest.perf
    hotspot

    # Formatter
    python312
    python312Packages.black

    mdbook
  ];

  shellHook = ''
    alias gls-build-dbg="scons target=editor tests=yes use_llvm=yes debug_symbols=yes"
    alias gls-build-compiledb="scons compiledb target=editor tests=yes use_llvm=yes"
    alias gls-tests="godot4 --luau-tests -- --skip-benchmarks"
    alias gls-perf="perf record --call-graph dwarf godot4"
  '';
}
