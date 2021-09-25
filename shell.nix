{ pkgs ? import <nixpkgs> { }
, srcDir ? null
}:

(pkgs.callPackage ./default.nix { inherit srcDir; }).overrideAttrs (old: {

  nativeBuildInputs = old.nativeBuildInputs ++ [

    pkgs.editorconfig-checker

    pkgs.nixpkgs-fmt

    (pkgs.python3.withPackages (ps: [
      ps.pytest
    ]))

  ];

})
