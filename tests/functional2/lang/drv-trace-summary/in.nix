with import ./config.nix;
let
  transitive-dependency = mkDerivation {
    name = "transitive-dependency";
    __structuredAttrs = true;

    nativeBuildInputs = [
      null
      (throw "transitive dependency growls")
    ];
  };


  direct-dependency = derivation {
    name = "direct-dependency";
    __structuredAttrs = true;

    libtrans = transitive-dependency;
  };

  package-you-care-about = derivation {
    name = "package-you-care-about";
    __structuredAttrs = true;

    buildInputs = [
      direct-dependency
    ];
  };

in package-you-care-about.outPath
