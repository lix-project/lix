let
  set = {
    inner = throw "nested throw";
  };
in
throw set.inner
