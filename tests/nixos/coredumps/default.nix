let
  inherit (import ../util.nix) mkNixBuildTest;
in mkNixBuildTest rec {
  name = "coredumps";
  extraMachineConfig = { pkgs, ... }: {
    boot.kernel.sysctl."kernel.core_pattern" = "core";
  };

  expressionFile = ./package.nix;

  testScriptPost = ''
    # do a test, but this time with coredumps enabled.
    machine.succeed('nix-build --option enable-core-dumps true --expr "let pkgs = import <nixpkgs> {}; in pkgs.callPackage ${expressionFile} { shouldBePresent = true; }"')
  '';
}
