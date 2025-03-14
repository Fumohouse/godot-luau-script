{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell.override { stdenv = pkgs.clang19Stdenv; } {
  nativeBuildInputs = with pkgs; [
    scons
    clang-tools

    # Formatter
    python312
    python312Packages.black
  ];

  shellHook = ''
    alias gls-build-dbg="scons target=editor tests=yes use_llvm=yes debug_symbols=yes"
    alias gls-build-compiledb="scons compiledb target=editor tests=yes use_llvm=yes"
  '';
}
