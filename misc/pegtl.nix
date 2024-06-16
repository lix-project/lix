{
  stdenv,
  cmake,
  ninja,
  fetchFromGitHub,
}:

stdenv.mkDerivation {
  pname = "pegtl";
  version = "3.2.7";

  src = fetchFromGitHub {
    repo = "PEGTL";
    owner = "taocpp";
    rev = "refs/tags/3.2.7";
    hash = "sha256-IV5YNGE4EWVrmg2Sia/rcU8jCuiBynQGJM6n3DCWTQU=";
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];
}
