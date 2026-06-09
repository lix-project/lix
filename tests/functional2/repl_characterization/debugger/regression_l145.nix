with { inherit ({}) invalid; };
let
  x = builtins.break 1;
in
  x
