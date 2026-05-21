# Basic repl test

Disable error traces, because we have the `show-trace` setting enabled
```nix
:te
```
```output
not showing error traces

```
trivial addition:

```nix
1 + 1
```
```output
2

```

Lets check if docs work
```nix
:doc builtins.add
```
```output
Synopsis: builtins.add e1 e2


    Return the sum of the numbers e1 and e2.

```

## Error printing

no trace by default
```nix
f = a: "" + a
f 2
```
```output
Added f.

error:
       … while concatenating
         at «string»:1:11:
            1| f = a: "" + a
             |           ^

       error: cannot coerce an integer to a string: 2

```

show trace when enabled
~~~nix
:te
f 2
~~~
~~~output
showing error traces

error:
       … from call site
         at «string»:1:1:
            1| f 2
             | ^

       … while calling anonymous lambda
         at «string»:1:5:
            1| f = a: "" + a
             |     ^

       … while concatenating
         at «string»:1:11:
            1| f = a: "" + a
             |           ^

       error: cannot coerce an integer to a string: 2

~~~

test if markdown does things correctly

```nix
:p __replaceStrings ["a"] ["`"] "\naaa\n"
```
````output

```

````
