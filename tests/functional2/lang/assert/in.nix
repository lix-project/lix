let
  x = arg: assert arg == "y"; 123;
in
x "x"
