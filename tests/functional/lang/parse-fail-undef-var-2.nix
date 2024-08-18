let

  f = {x, y : ["baz" "bar" z "bat"]}: x + y;

in
f {x = "foo"; y = "bar";}
