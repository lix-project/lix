{
  systems ? [ "x86_64-linux" ],
  nixpkgs ? <nixpkgs>,
  lixSrc ? builtins.fetchGit ./.,
}:

let
  nixpkgs' =
    if builtins.isAttrs nixpkgs then
      nixpkgs
    else
      {
        outPath = nixpkgs;
        rev = "0000000000000000000000000000000000000000";
        shortRev = "0000000";
        lastModifiedDate = "19700101000000";
      };
in
let
  nixpkgs = nixpkgs';
in
let
  flakeLock = builtins.fromJSON (builtins.readFile (lixSrc + "/flake.lock"));

  fetchFlakeLockInputs =
    names:
    lib.listToAttrs (
      map (
        inputName:
        let
          nodeName = flakeLock.nodes.${flakeLock.root}.inputs.${inputName};
        in
        lib.nameValuePair inputName (builtins.fetchTree flakeLock.nodes.${nodeName}.locked)
      ) names
    );

  lib = import (nixpkgs + "/lib");
  filterSystems = lib.filter (n: builtins.elem n systems);

  inputs =
    (import (lixSrc + "/nix-support/build/inputs.nix") (
      fetchFlakeLockInputs [
        "nix2container"
        "nixpkgs-regression"
        "nix_2_18"
      ]
      // {
        inherit lib nixpkgs lixSrc;
      }
    )).overrideScope
      (
        _: prev: {
          linux32BitSystems = filterSystems prev.linux32BitSystems;
          linux64BitSystems = filterSystems prev.linux64BitSystems;
          darwinSystems = filterSystems prev.darwinSystems;
        }
      );

  outputs = inputs.callPackage (lixSrc + "/nix-support/build/outputs.nix") { };
in
lib.fix (
  self:
  outputs.ciArtifacts
  // {
    inherit (outputs) tests;

    # Aggregate job that is finished in Hydra _after_ all constituent jobs (here: grouped by system)
    # succeed.
    # This is used to run CD scripts once all builds are finished on Hydra.
    release = inputs.forAllSystems (
      system:
      let
        pkgs = inputs.nixpkgsFor.${system}.native;
      in
      pkgs.runCommand "release"
        {
          _hydraAggregate = true;
          constituents = lib.filter (x: x != null) (
            lib.mapAttrsToListRecursiveCond
              (_: val: !(lib.isDerivation val || builtins.any (system': val ? ${system'}) systems))
              (
                path: drv:
                if drv ? ${system} then
                  lib.concatStringsSep "." (path ++ [ system ])
                else if drv.system or null == system then
                  lib.concatStringsSep "." path
                else
                  null
              )
              (
                removeAttrs self [
                  "release"
                ]
              )
          );
        }
        ''
          touch $out
        ''
    );
  }
)
