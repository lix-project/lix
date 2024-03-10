assert (
  let x = [ 1 [ 2 3 4 5 6 7 8 9 x 10 11 ] 12 ];
  in builtins.deepSeq x x
); 1
