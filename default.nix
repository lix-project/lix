{ stdenv
, lib
, nix
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
    (path: type: type != "directory" || baseNameOf path != "build")
    dir;
in
stdenv.mkDerivation rec {
  pname = "nix-eval-jobs";
  version = "2.16.0";
  src = if srcDir == null then filterMesonBuild ./. else srcDir;
  buildInputs = [
    nlohmann_json
    nix
    boost
  ];
  nativeBuildInputs = [
    meson
    pkg-config
    ninja
    # nlohmann_json can be only discovered via cmake files
    cmake
  ];

  meta = {
    description = "Hydra's builtin hydra-eval-jobs as a standalone";
    homepage = "https://github.com/nix-community/nix-eval-jobs";
    license = lib.licenses.gpl3;
    maintainers = with lib.maintainers; [ adisbladis mic92 ];
    platforms = lib.platforms.unix;
  };
}
