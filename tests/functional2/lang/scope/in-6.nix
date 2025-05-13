let

  f = {x ? y, y ? x}: x + y;
in
f {x = "c";} + f {y = "d";}
