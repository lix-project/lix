let
  positive = n: acc: if n == 0 then [] else [ acc ] ++ positive (n - 1) (acc * 2 + 1);
  negative = n: acc: if n == 0 then [] else [ acc ] ++ negative (n - 1) (acc * 2);
in
[
  (positive 64 0)
  (negative 64 (-1))
]
