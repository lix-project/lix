let
  inherit (import ../util.nix) mkNixBuildTest;
in mkNixBuildTest {
  name = "root-in-sandbox";
  extraMachineConfig = { pkgs, ... }: {
    security.wrappers.ohno = {
      owner = "root";
      group = "root";
      setuid = true;
      source = "${pkgs.coreutils}/bin/whoami";
    };
    nix.settings.extra-sandbox-paths = ["/run/wrappers/bin"];
  };
  expressionFile = ./package.nix;
}
