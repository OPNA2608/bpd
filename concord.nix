{ stdenv
, lib
, fetchFromGitHub
, buildPackages
, curl
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "concord";
  version = "2.2.1";

  src = fetchFromGitHub {
    owner = "Cogmasters";
    repo = "concord";
    rev = "v${finalAttrs.version}";
    hash = "sha256-8k/W6007U1/s3vx03i1929a5RKZtpW/jOr4JDwmzwp8=";
  };

  postPatch = ''
    # Fix cross
    substituteInPlace gencodecs/Makefile \
      --replace 'CC' 'HOST_CC' \
      --replace 'CPP' 'HOST_CPP' \
      --replace '$(HOST_CC) -c $(CFLAGS) $< -o $@' '$(CC) -c $(CFLAGS) $< -o $@'
  '';

  strictDeps = true;

  pkgsBuildBuild = [
    buildPackages.stdenv.cc
  ];

  propagatedBuildInputs = [
    curl
  ];

  enableParallelBuilding = true;

  env.NIX_CFLAGS_COMPILE = toString [
    "-DCCORD_SIGINTCATCH"
    "-O0"
    "-g"
  ];

  makeFlags = [
    (if stdenv.hostPlatform.isStatic then "static" else "shared")
  ];

  hardeningDisable = [ "fortify" ];
  dontStrip = true;

  installFlags = [
    "PREFIX=$(out)"
  ];
})
