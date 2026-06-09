---
args: ['-v']
---

Adding variables gives simple user feedback

```nix
foo = 5
foo = 10
```
```output
Added foo.

Updated foo.

```

Optional semicolon at the end, allow setting multiple variables in one line

```nix
foo = 2;
foo = 2; bar = 3;
```
```output
Updated foo.

Updated foo.
Added bar.

```

String identifiers work

```nix
"silly name" = null
```
```output
Added "silly name".

```

Attrset syntax works, but without dynamic attrs or merging

```nix
foo.bar = "baz"
foo
foo."this works" = 42
foo
foo.bar = "baz"; foo.more = "error"
${foo} = 10
```
```output
Updated foo.

{ bar = "baz"; }

Updated foo.

{ "this works" = 42; }

error: attribute 'foo' already defined at «string»:1:18
       at «string»:1:12:
            1| foo.bar = "baz"; foo.more = "error"
             |            ^

error: dynamic attributes not allowed in REPL
       at «string»:1:1:
            1| ${foo} = 10
             | ^

```
