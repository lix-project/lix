let
  a = builtins.substring 1;
  b = a 3;
in
b "1234567890"
