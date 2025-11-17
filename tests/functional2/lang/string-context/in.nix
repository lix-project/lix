let s = "foo ${builtins.substring 33 100 (baseNameOf "${./in.nix}")} bar";
in
  if s != "foo in.nix bar"
  then abort "context not discarded"
  else builtins.unsafeDiscardStringContext s

