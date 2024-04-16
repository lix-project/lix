{ pkgs, ... }:
let
  # Can't use the cool helper because inputDerivation does not work with FODs :(
  checkResolvconfInSandbox = pkgs.runCommand "resolvconf-works-in-sandbox" {
    # must be an FOD to have a resolv.conf in the first place
    outputHash = "sha256-47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=";
    outputHashAlgo = "sha256";
    outputHashType = "flat";
  } ''
    cat /etc/resolv.conf
    touch $out
  '';
in {
  name = "symlink-resolvconf";

  nodes.machine = {
    # Enabling resolved makes /etc/resolv.conf a symlink to /etc/static/resolv.conf, which is itself a symlink to /run.
    # If this works, most other things probably will too.
    services.resolved.enable = true;

    virtualisation.additionalPaths = [checkResolvconfInSandbox.drvPath];
  };

  testScript = { nodes }: ''
    start_all()

    machine.succeed('nix-build --check ${checkResolvconfInSandbox.drvPath}')
  '';
}
