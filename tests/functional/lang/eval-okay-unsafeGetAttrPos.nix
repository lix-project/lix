let
  pos = builtins.unsafeGetAttrPos "y" (import ./eval-okay-unsafeGetAttrPos.imported-nix);
in
pos
