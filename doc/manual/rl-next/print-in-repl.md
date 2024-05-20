---
synopsis: "REPL printing improvements"
prs: [9931, 10208]
cls: [375, 492]
credits: [9999years, horrors]
category: Improvements
---

The REPL printer has been improved to do the following:
- If a string is passed to `:print`, it is printed literally to the screen
- Structures will be printed as multiple lines when necessary

Before:

```
nix-repl> { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
{ attrs = { ... }; list = [ ... ]; list' = [ ... ]; }

nix-repl> :p { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
{ attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }

nix-repl> :p "meow"
"meow"
```

After:

```
nix-repl> { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
{
  attrs = { ... };
  list = [ ... ];
  list' = [ ... ];
}

nix-repl> :p { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
{
  attrs = {
    a = {
      b = {
        c = { };
      };
    };
  };
  list = [ 1 ];
  list' = [
    1
    2
    3
  ];
}

nix-repl> :p "meow"
meow
```
