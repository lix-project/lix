# FIXME: upstream to nixpkgs (do NOT build with gcc due to gcc coroutine bugs)
{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  openssl,
  zlib,
}:
assert stdenv.cc.isClang;
stdenv.mkDerivation rec {
  pname = "capnproto";
  version = "1.0.2";

  # release tarballs are missing some ekam rules
  src = fetchFromGitHub {
    owner = "capnproto";
    repo = "capnproto";
    rev = "v${version}";
    sha256 = "sha256-LVdkqVBTeh8JZ1McdVNtRcnFVwEJRNjt0JV2l7RkuO8=";
  };

  nativeBuildInputs = [ cmake ];
  propagatedBuildInputs = [
    openssl
    zlib
  ];

  # FIXME: separate the binaries from the stuff that user systems actually use
  # This runs into a terrible UX issue in Lix and I just don't want to debug it
  # right now for the couple MB of closure size:
  # https://git.lix.systems/lix-project/lix/issues/551
  # outputs = [ "bin" "dev" "out" ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_SHARED_LIBS" true)
    # Take optimization flags from CXXFLAGS rather than cmake injecting them
    (lib.cmakeFeature "CMAKE_BUILD_TYPE" "None")
  ];

  env = {
    # Required to build the coroutine library
    CXXFLAGS = "-std=c++20";
  };

  separateDebugInfo = true;

  meta = with lib; {
    homepage = "https://capnproto.org/";
    description = "Cap'n Proto cerealization protocol";
    longDescription = ''
      Cap’n Proto is an insanely fast data interchange format and
      capability-based RPC system. Think JSON, except binary. Or think Protocol
      Buffers, except faster.
    '';
    license = licenses.mit;
    platforms = platforms.all;
    maintainers = lib.teams.lix.members;
  };
}
