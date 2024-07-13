# Usually "experimental" or "deprecated"
_kind:
# "xp" or "dp"
kindShort:

with builtins;
with import ./utils.nix;

let
  showFeature =
    name: doc:
    squash ''
      ## [`${name}`]{#${kindShort}-feature-${name}}

      ${doc}
    '';
in
xps: (concatStringsSep "\n" (attrValues (mapAttrs showFeature xps)))
