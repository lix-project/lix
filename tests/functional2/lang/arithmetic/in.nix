with import ./lib.nix;

let

  /* Supposedly tail recursive version:

  range_ = accum: first: last:
    if first == last then ([first] ++ accum)
    else range_ ([first] ++ accum) (builtins.add first 1) last;

  range = range_ [];
  */

  x = 12;

  err = abort "urgh";

in

assert 3 < 7;
assert !(7 < 3);
assert !(3 < 3);

assert 3 <= 7;
assert !(7 <= 3);
assert 3 <= 3;

assert !(3 > 7);
assert 7 > 3;
assert !(3 > 3);

assert !(3 >= 7);
assert 7 >= 3;
assert 3 >= 3;

assert 2 > 1 == 1 < 2;
assert 1 + 2 * 3 >= 7;
assert !(1 + 2 * 3 < 7);

# Not integer, but so what.
assert "aa" < "ab";
assert !("aa" < "aa");
assert "foo" < "foobar";
[
  (sum (range 1 50))
  (123 + 456)
  (0 + -10 + -(-11) + -x)
  (10 - 7 - -2)
  (10 - (6 - -1))
  (10 - 1 + 2)
  (3 * 4 * 5)
  (56088 / 123 / 2)
  (3 + 4 * const 5 0 - 6 / id 2)

  (builtins.bitAnd 12 10) # 0b1100 & 0b1010 =  8
  (builtins.bitOr  12 10) # 0b1100 | 0b1010 = 14
  (builtins.bitXor 12 10) # 0b1100 ^ 0b1010 =  6
]

