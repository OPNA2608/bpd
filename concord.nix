{ stdenv
, lib
, fetchFromGitHub
, fetchpatch
, buildPackages
, curl
, CoreServices
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "concord";
  #version = "2.2.1";
  version = "unstable-2023-08-05-fix-unexpected-shutdowns"; # https://github.com/Cogmasters/concord/pull/155

  src = fetchFromGitHub {
    owner = "Cogmasters";
    repo = "concord";
    #rev = "v${finalAttrs.version}";
    #hash = "sha256-8k/W6007U1/s3vx03i1929a5RKZtpW/jOr4JDwmzwp8=";
    rev = "fa122d05f1f85e3f68c5bc35ab01b7d7f564a0ef";
    hash = "sha256-J9X3OVt1E/CDBQZjlAcQ2UKdSn8zdnmWhLH0VQ5uXh4=";
  };

  #patches = [
  #  (fetchpatch {
  #    name = "0001-concord-use-libcurls-custom-free.patch";
  #    url = "https://github.com/Cogmasters/concord/pull/153/commits/357b5b0437a809bb28dc55c07f42d3bbdb15340d.patch";
  #    hash = "sha256-EdnEc7FPgNv9CJMsGwXJ1amq533XxuZHTk4HWXzF2bE=";
  #  })
  #  (fetchpatch {
  #    name = "0002-concord-fix-sudden-shutdowns.patch";
  #    url = "https://github.com/Cogmasters/concord/commit/95304b7766fe72be5d6c25eef7fd7916fb862c33.patch";
  #    hash = "sha256-gD1flwp2VAVFx9gADtXxWWcxEiA9f5kq5X6Jhd4kqwM=";
  #  })
  #];

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
    (curl.overrideAttrs (oa: {
      dontStrip = true;
      configureFlags = (oa.configureFlags or []) ++ [
        "--enable-debug"
      ];
    }))
  ];

  buildInputs = lib.optionals stdenv.hostPlatform.isDarwin [
    CoreServices
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

  postFixup = lib.optionalString stdenv.hostPlatform.isDarwin ''
    install_name_tool -id $out/lib/libdiscord.so $out/lib/libdiscord.so
  '';
})
