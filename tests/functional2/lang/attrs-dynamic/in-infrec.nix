let
  lenName = x: toString (builtins.length (builtins.attrNames x));
  b = { ${lenName a} = null; };
  a = { ${lenName b} = null; };

in

[ a b ]
