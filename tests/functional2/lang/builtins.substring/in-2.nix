with builtins;

let

  s = "foobar";

in
[
  (substring 1 2 s)
  (substring 0 (stringLength s) s)
  (substring 3 100 s)
  (substring 2 (sub (stringLength s) 3) s)
  (substring 3 0 s)
  (substring 3 1 s)
  (substring 5 10 "perl")
  (substring 3 (-1) "tebbad")
]