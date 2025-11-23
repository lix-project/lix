# Deep recursive attr merges
# Regression test for https://git.lix.systems/lix-project/lix/issues/845 / https://github.com/NixOS/nix/issues/11268

# Also this is an eval test and not a parser test because the printed value is easier to inspect than the AST,
# though we are primarily testing parser functionality here
let
  reference = { a.b.c = 1; a.b.d = 2; };
in
# Test cases courtesy of @rhendric
assert { a = { b = { c = 1; }; }; a = { b = { d = 2; }; }; } == reference;
assert { a.b = { c = 1; }; a.b = { d = 2; }; } == reference;
assert { a = { b.c = 1; }; a = { b.d = 2; }; } == reference;
assert { a = { b = { c = 1; }; }; a.b.d = 2; } == reference;
assert { a.b.c = 1; a = { b = { d = 2; }; }; } == reference;
{
  foo.bar.baz = 1;
  foo = {
    bar.qux = 2;
  };
}
