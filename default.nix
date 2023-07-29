{ stdenv
, lib
, callPackage
, cmake
, concord ? null
, json_c
}:

let
  concord' = if (concord != null) then concord else callPackage ./concord.nix {
    inherit stdenv;
  };
in
stdenv.mkDerivation (finalAttrs: {
  pname = "bpd";
  version = "0.0.0-local";

  src = lib.sources.cleanSourceWith {
    filter = name: type: lib.sources.cleanSourceFilter name type && ! (
      # VCS
      (lib.strings.hasSuffix ".gitignore" (baseNameOf name)) ||
      # editor stuff
      (lib.strings.hasSuffix ".editorconfig" (baseNameOf name)) ||
      # Nix stuff
      (lib.strings.hasSuffix ".nix" (baseNameOf name)) ||
      # logs
      (lib.strings.hasSuffix ".log" (baseNameOf name)) ||
      # end-user config
      (lib.strings.hasSuffix ".json" (baseNameOf name))
    );
    src = ./.;
  };

  strictDeps = true;

  cmakeBuildType = "Debug";
  dontStrip = true;

  nativeBuildInputs = [
    cmake
  ];

  buildInputs = [
    concord'
    json_c
  ];
})
