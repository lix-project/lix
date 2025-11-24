{
  lib,
  runCommand,
  nix,

  nixpkgs-regression,
}:

let
  inherit (lib) concatStringsSep;

  # FIXME: All of these are fixed in Nixpkgs already, so clear the list on the next `nixpkgs-regression` bump
  deprecatedFeatures = [
    "broken-string-indentation"
    "broken-string-escape"
    "rec-set-merges"
    "rec-set-dynamic-attrs"
  ];
in

runCommand "eval-nixos"
  {
    buildInputs = [ nix ];
    env.NIX_CONFIG = "extra-deprecated-features = ${concatStringsSep " " deprecatedFeatures}";
  }
  ''
    type -p nix-env
    # Note: we're filtering out nixos-install-tools because https://github.com/NixOS/nixpkgs/pull/153594#issuecomment-1020530593.
    time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
    [[ $(sha1sum < packages | cut -c1-40) = 402242fca90874112b34718b8199d844e8b03d12 ]]
    mkdir $out
  ''
