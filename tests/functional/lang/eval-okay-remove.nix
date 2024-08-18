let
  attrs = {x = 123; y = 456;};
in
(removeAttrs attrs ["x"]).y
