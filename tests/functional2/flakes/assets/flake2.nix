{
  outputs =
    { self }:
    {
      x = builtins.readFile ../file;
    };
}
