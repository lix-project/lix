let
  pos = builtins.unsafeGetAttrPos "y" (import ./imported.nix);
in
pos
