info: final: prev:
{
  var = prev.var + "b";

  # We can access the final value of `var` here even though it isn't defined yet:
  varUsingFinal = "final value is: " + final.newVar;
}
