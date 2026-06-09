Printing a string with escapes in it will render as a string normally

```nix
"meow\n\nmeowmeowmeow"
```
```output
"meow\n\nmeowmeowmeow"

```


But with `:p` on the string itself it will print it literally to the output
```nix
:p "meow\n\nmeowmeow"
```
```output
meow

meowmeow

```



Also, `:p` will expand attrs, but it will leave the strings escaped as normal if they aren't at the top level item being printed

```nix
builtins.listToAttrs (builtins.genList (x: { name = "meow${toString x}"; value = { meow = { inherit x; s = "meowmeow\n\n${toString x}"; }; }; }) 10)
:p builtins.listToAttrs (builtins.genList (x: { name = "meow${toString x}"; value = { meow = { inherit x; s = "meowmeow\n\n${toString x}"; }; }; }) 10)
```
```output
{
  meow0 = { ... };
  meow1 = { ... };
  meow2 = { ... };
  meow3 = { ... };
  meow4 = { ... };
  meow5 = { ... };
  meow6 = { ... };
  meow7 = { ... };
  meow8 = { ... };
  meow9 = { ... };
}

{
  meow0 = {
    meow = {
      s = "meowmeow\n\n0";
      x = 0;
    };
  };
  meow1 = {
    meow = {
      s = "meowmeow\n\n1";
      x = 1;
    };
  };
  meow2 = {
    meow = {
      s = "meowmeow\n\n2";
      x = 2;
    };
  };
  meow3 = {
    meow = {
      s = "meowmeow\n\n3";
      x = 3;
    };
  };
  meow4 = {
    meow = {
      s = "meowmeow\n\n4";
      x = 4;
    };
  };
  meow5 = {
    meow = {
      s = "meowmeow\n\n5";
      x = 5;
    };
  };
  meow6 = {
    meow = {
      s = "meowmeow\n\n6";
      x = 6;
    };
  };
  meow7 = {
    meow = {
      s = "meowmeow\n\n7";
      x = 7;
    };
  };
  meow8 = {
    meow = {
      s = "meowmeow\n\n8";
      x = 8;
    };
  };
  meow9 = {
    meow = {
      s = "meowmeow\n\n9";
      x = 9;
    };
  };
}

```



Printing an environment with :env after adding a variable to the scope
```nix
foo = "bar"
:env
```
```output
Added foo.

Env level 0
static: foo 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

```


Printing a derivation or something
```nix
fakeDrv = let drvAttrs = { builder = "meow"; system = "meower"; name = "mrowmrow"; }; in { inherit (drvAttrs) builder system name; inherit drvAttrs; type = "derivation"; }
:p fakeDrv
```
```output
Added fakeDrv.

{
  builder = "meow";
  drvAttrs = «3 attributes elided»;
  name = "mrowmrow";
  system = "meower";
  type = "derivation";
}

```
