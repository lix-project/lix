let
  as = { x = 123; y = 456; } // { z = 789; } // { z = 987; };

  A = "a";
  Z = "z";

in
assert !(builtins.hasAttr A as);
assert builtins.hasAttr Z as;
builtins.getAttr Z as
