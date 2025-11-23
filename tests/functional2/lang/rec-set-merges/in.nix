{
  foo = {
    # this attribute set is not recursive
    bar = 1;
  };
  foo = rec {
    # merging this attrset to foo.bar, discards the `rec` modifier
    x = 1;
    y = x;
  };
}
