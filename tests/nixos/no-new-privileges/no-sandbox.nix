let
  inherit (import ../util.nix) mkNixBuildTest;
in
mkNixBuildTest {
  name = "no-new-privileges-outside-sandbox";
  extraMachineConfig =
    { pkgs, ... }:
    {
      security.wrappers.ohno = {
        owner = "root";
        group = "root";
        capabilities = "cap_sys_nice=eip";
        source = "${pkgs.libcap}/bin/getpcaps";
      };
      nix.settings = {
        extra-sandbox-paths = [ "/run/wrappers/bin/ohno" ];
        sandbox = false;
      };
    };
  expressionFile = ./package.nix;
}
