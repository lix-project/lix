assert (
  let x = { a.a.a.a.a.a.a.a.a = { a.a = 1; b = 2; }; a.b.c.x = 3; c = 4; };
  in builtins.deepSeq x x
); 1
