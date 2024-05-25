let
  inherit (import ../util.nix) mkNixBuildTest;
in
mkNixBuildTest {
  name = "no-new-privileges-in-sandbox";
  extraMachineConfig =
    { pkgs, ... }:
    {
      security.wrappers.ohno = {
        owner = "root";
        group = "root";
        capabilities = "cap_sys_nice=eip";
        source = "${pkgs.libcap}/bin/getpcaps";
      };
      nix.settings.extra-sandbox-paths = [ "/run/wrappers/bin/ohno" ];
    };
  expressionFile = ./package.nix;
}
