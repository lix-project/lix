# Upstreaming here, can be deleted once it's upstreamed:
# https://github.com/NixOS/nixpkgs/pull/297102
{
  stdenv,
  lib,
  cmake,
  fetchFromGitHub,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "clangbuildanalyzer";
  version = "1.5.0";

  src = fetchFromGitHub {
    owner = "aras-p";
    repo = "ClangBuildAnalyzer";
    rev = "v${finalAttrs.version}";
    sha256 = "sha256-kmgdk634zM0W0OoRoP/RzepArSipa5bNqdVgdZO9gxo=";
  };

  nativeBuildInputs = [ cmake ];

  meta = {
    description = "Tool for analyzing Clang's -ftrace-time files";
    homepage = "https://github.com/aras-p/ClangBuildAnalyzer";
    maintainers = with lib.maintainers; [ lf- ];
    license = lib.licenses.unlicense;
    platforms = lib.platforms.unix;
    mainProgram = "ClangBuildAnalyzer";
  };
})
