# File which contains all possible syntax constructs in order to fully test the JSON output from `nix-instantiate --parse`

# ExprWith
with {};
# ExprList
[
  # ExprInt
  0
  # ExprFloat
  0.0
  # ExprString
  ""
  "foo"
  "foo\nbar"
  # ExprPath
  /root
  # ExprVar
  var1
  # ExprSelect
  foo.bar or baz
  # ExprOpHasAttr
  (foo ? bar.baz)
  # ExprSet (ExprAttrs, ExprInheritFrom, ...)
  { }
  rec { }
  {
    attr1 = value1;
    attr2 = null;
    inherit foo;
    inherit (bar) baz;
    inherit (complicated [ expression ]) thing;
    inherit;
    "string attr" = "string value";
    ${"fake dynamic attr"} = 42;
    ${dynamicAttr}.${"anotherOne" + ""} = null;
    attr3.nested = { more = 0; };
    attr3.nested.merged = 1;
  }
  rec {
    attr1 = value1;
    attr2 = null;
    inherit foo;
    inherit (bar) baz;
    inherit (complicated [ expression ]) thing;
    inherit;
    "string attr" = "string value";
    ${"fake dynamic attr"} = 42;
    ${dynamicAttr}.${"anotherOne" + ""} = null;
    attr3.nested = { more = 0; };
    attr3.nested.merged = 1;
  }
  (
    # ExprLet
    let in
    let
      attr1 = value1;
      attr2 = null;
      inherit foo;
      inherit (bar) baz;
      inherit (complicated [ expression ]) thing;
      inherit;
      "string attr" = "string value";
      ${"fake dynamic attr"} = 42;
      attr3.nested = { more = 0; };
      attr3.nested.merged = 1;
    in
    # ExprAssert
    assert false;
    assert true;
    assert let in (myFunction {}.overrideStuff) == 4.2;
    [ ]
  )
  # ExprLambda
  (x: x)
  (x: y: z: z y x)
  ({}: null)
  ({...}: null)
  ({}@arg: null)
  ({foo ? null, bar, baz}@all: all)
  (all@{foo ? null, ...}: all)
  # ExprIf
  (if null then 0 else 1)
  # ExprOpNot
  (!true)
  # ExprOpEq, ExprOpNEq, ExprOpAnd, ExprOpOr, ExprOpImpl, and general arithmetic
  (7 + (-5) * 12 / 3 - 1)
  (0 + -10 + -(-11) + -x)
  (if 2 > 1 == 1 < 2 -> 2 >= 1 != 1 <= 2 && true || false then 1 else err)
  # ExprOpUpdate
  (foo // {} // bar.baz)
  # ExprOpConcatLists
  (foo ++ [] ++ optional cond value)
  # ExprConcatStrings
  (1 + 2 + 3)
  "${""}"
  "Foo ${3 + "2"}"
  # ExprPos
  __curPos
]
