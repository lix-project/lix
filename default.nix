{ stdenv
, nixFlakes
, meson
, cmake
, ninja
, pkg-config
, boost
, nlohmann_json
, srcDir ? ./.
}:

stdenv.mkDerivation rec {
  pname = "hydra-eval-jobs";
  version = "0.0.1";
  src = srcDir;
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
    homepage = "https://github.com/Mic92/hydra-eval-jobs";
    license = licenses.mit;
    maintainers = with maintainers; [ mic92 ];
    platforms = platforms.unix;
  };
}
