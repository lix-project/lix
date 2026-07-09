let
  fib = n:
    assert n > 0;
    builtins.addErrorContext
      "computing fib(${toString n})"
      # note: we use builtins.add explicitly because it creates more frames than (+)
      (builtins.add (fib (n - 1)) (fib (n - 2)));
in
  fib 4
