# parser tests serialize the AST as json, and json does not like non-utf8 strings at all
{
  invalidString = "vÃ¤lid bit ÿ morÃ© vÃ¥lid";
  invalidAttrPath = x: x.a."aÿb".c;
  "invalidÿName" = 1;
  invalidInherit = x: with x; { inherit "aÿb"; };
  invalidInheritFrom = x: { inherit (x) "aÿb"; };
  invalidLet = let "aÿb" = 1; in 1;
}
