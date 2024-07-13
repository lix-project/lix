# Usually "experimental" or "deprecated"
kind:
# "xp" or "dp"
kindShort:

with builtins;
with import ./utils.nix;

let
  showExperimentalFeature = name: doc: ''
    - [`${name}`](@docroot@/contributing/${kind}-features.md#${kindShort}-feature-${name})
  '';
in
xps: indent "  " (concatStrings (attrValues (mapAttrs showExperimentalFeature xps)))
