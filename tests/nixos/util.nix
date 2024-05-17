{
  mkNixBuildTest =
    { name, expressionFile, extraMachineConfig ? {}, testScriptPre ? "", testScriptPost ? "" }:
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

        ${testScriptPre}

        machine.succeed('nix-build --expr "let pkgs = import <nixpkgs> {}; in pkgs.callPackage ${expressionFile} {}"')

        ${testScriptPost}
      '';
    };
}
