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
    alias gls-build-dbg="SCONS_CACHE=build/dbg scons target=editor tests=yes use_llvm=yes debug_symbols=yes"
    alias gls-build-relwithdbg="SCONS_CACHE=build/relwithdbg scons target=template_release tests=yes use_llvm=yes debug_symbols=yes"
    alias gls-build-compiledb="scons compiledb target=editor tests=yes use_llvm=yes"
    alias gls-tests="pushd test_project/ && godot4 --luau-tests --headless -- --skip-benchmarks && popd"
    alias gls-perf="pushd test_project/ && perf record --call-graph dwarf godot4 && hotspot && popd"
  '';
}
