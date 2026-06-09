Errors at the top of an expression are printed normally:

```nix
builtins.throw "Evil puppy detected!!!"
```
```output
error:
       … caused by explicit throw
         at «string»:1:1:
            1| builtins.throw "Evil puppy detected!!!"
             | ^

       error: Evil puppy detected!!!

```


Errors in attribute values are printed inline, to make it easier to explore
values like nixpkgs where some parts of the value fail to evaluate:

```nix
{ puppy = builtins.throw "This puppy is EVIL!!!"; puppy2 = "This puppy is GOOD :)"; }
```
```output
{
  puppy = «error: This puppy is EVIL!!!»;
  puppy2 = "This puppy is GOOD :)";
}

```


Same for list values:

```nix
[ (builtins.throw "This puppy is EVIL!!!") ("This puppy is GOOD :)") ]
```
```output
[
  «error: This puppy is EVIL!!!»
  "This puppy is GOOD :)"
]

```
