{ pkgsSrc ? builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/3641464f1c32bb566cc195696e73d1e784a2657d.tar.gz";
    sha256 = "0jhxmy05y6rap1f1mlixg2sqk3s84kgjml9120shqvffhy31ghdb";
  }
, pkgs ? import pkgsSrc {}
}:

let
  targetPackage = pkgs.callPackage ./default.nix { };
  addIfAvailable = pkg: with pkgs;
    lib.optional (lib.meta.availableOn stdenv.hostPlatform pkg && pkg.meta.broken != true) pkg;
in
pkgs.mkShell.override {
} {
  name = "bpd-devenv";

  nativeBuildInputs = with pkgs; [
    gdb
    pkg-config
    (addIfAvailable valgrind)

    # Bot requirements
    ffmpeg
    vgmplay-libvgm
    wget
  ] ++ targetPackage.nativeBuildInputs;

  inherit (targetPackage) buildInputs;

  shellHook = ''
    alias do-build="nix-build --no-out-link -E 'with import ${pkgsSrc} { }; callPackage ./default.nix { }'"
    export NIX_CFLAGS_COMPILE="$(pkg-config --cflags json-c) -DDLAR_SCRIPT=\"\" -D_POSIX_C_SOURCE=200809L $NIX_CFLAGS_COMPILE"
  '';
}
