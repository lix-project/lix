let
  inherit (import ../util.nix) mkNixBuildTest;
in
mkNixBuildTest {
  name = "io_uring";
  expressionFile = ./package.nix;
}
