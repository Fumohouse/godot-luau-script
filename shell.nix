{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    scons
    clang

    # Formatter
    python312
    python312Packages.black
  ];
}
