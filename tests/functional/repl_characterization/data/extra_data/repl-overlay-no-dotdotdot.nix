let
  puppy = "doggy";
in
  {currentSystem}: final: prev: {
    inherit puppy;
  }
