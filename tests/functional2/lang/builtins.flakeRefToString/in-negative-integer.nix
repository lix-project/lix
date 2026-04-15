let
  n = -1;
in
builtins.flakeRefToString {
  type  = "github";
  owner = "NixOS";
  repo  = n;
  ref   = "23.05";
  dir   = "lib";
}
