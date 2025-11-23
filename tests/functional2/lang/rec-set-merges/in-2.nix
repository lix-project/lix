{
  foo.bar = 1;
  #   ^~~  we are implicitly creating a non-recursive attrset
  foo = rec {
    # merging this attrset to foo.bar, discards the `rec` modifier
    x = 1;
    y = x;
  };
}
