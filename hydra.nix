{ stdenv
, nixFlakes
, meson
, cmake
, ninja
, pkg-config
, boost
, nlohmann_json
, srcDir ? null
}:

let
  filterMesonBuild = dir: builtins.filterSource
    (path: type: type != "directory" || baseNameOf path != "build") dir;
in
stdenv.mkDerivation rec {
  pname = "nix-eval-jobs";
  version = "0.0.1";
  src = if srcDir == null then filterMesonBuild ./. else srcDir;
  buildInputs = [
    nlohmann_json nixFlakes boost
  ];
  nativeBuildInputs = [
    meson pkg-config ninja
    # nlohmann_json can be only discovered via cmake files
    cmake
  ];
  meta = with stdenv.lib; {
    description = "Hydra's builtin hydra-eval-jobs as a standalone";
    homepage = "https://github.com/nix-community/nix-eval-jobs";
    license = licenses.mit;
    maintainers = with maintainers; [ mic92 ];
    platforms = platforms.unix;
  };
}
