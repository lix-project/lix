{
  mkNixBuildTest = { name, expressionFile, extraMachineConfig ? {} }:
    { lib, pkgs, ... }:
    {
      inherit name;

      nodes.machine = {
        imports = [extraMachineConfig];
        nix.nixPath = ["nixpkgs=${pkgs.path}"];
        nix.settings.substituters = lib.mkForce [];
        virtualisation.additionalPaths = [
          expressionFile
          (pkgs.callPackage expressionFile {}).inputDerivation
        ];
      };

      testScript = { nodes }: ''
        start_all()

        machine.succeed('nix-build --expr "let pkgs = import <nixpkgs> {}; in pkgs.callPackage ${expressionFile} {}"')
      '';
    };
}
