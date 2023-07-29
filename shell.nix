{ pkgsSrc ? builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/3641464f1c32bb566cc195696e73d1e784a2657d.tar.gz";
    sha256 = "0jhxmy05y6rap1f1mlixg2sqk3s84kgjml9120shqvffhy31ghdb";
  }
, pkgs ? import pkgsSrc {}
}:

let
  targetPackage = pkgs.callPackage ./default.nix { };
in
pkgs.mkShell.override {
} {
  name = "discord-bot-thingy-devenv";

  nativeBuildInputs = with pkgs; [
    gdb
    pkg-config
    vgmplay-libvgm
    wget
  ] ++ targetPackage.nativeBuildInputs;

  inherit (targetPackage) buildInputs;

  shellHook = ''
    alias do-build="nix-build --no-out-link -E 'with import ${pkgsSrc} { }; callPackage ./default.nix { }'"
    export NIX_CFLAGS_COMPILE="$(pkg-config --cflags json-c) $NIX_CFLAGS_COMPILE"
  '';
}