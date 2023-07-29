{ stdenv
, lib
, fetchFromGitHub
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

	strictDeps = true;

	propagatedBuildInputs = [
		curl
	];

	enableParallelBuilding = true;

	makeFlags = [
    (if stdenv.hostPlatform.isStatic then "static" else "shared")
  ];

  preBuild = ''
    export CFLAGS="-DCCORD_SIGINTCATCH"
  '';

	installFlags = [
		"PREFIX=$(out)"
	];
})
