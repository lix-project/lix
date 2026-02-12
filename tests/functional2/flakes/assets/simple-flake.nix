{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.@system@ = rec {
      foo = import ./simple.nix;
      default = foo;
    };
    packages.someOtherSystem = rec {
      foo = import ./simple.nix;
      default = foo;
    };

    # To test "nix flake init".
    legacyPackages.@system@.hello = import ./simple.nix;
  };
}
