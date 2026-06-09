---
args: ['--debugger']
---

```nix
let f = _: throw "x_x"; x = f 5; in x
```
```output
error: x_x

Added 3 variables.
```


absolute indices still work:
```nix
:st 1
```
```output

1: while calling a function
«string»:1:12

     1| let f = _: throw "x_x"; x = f 5; in x
      |            ^

Env level 0
static: _ 

Env level 1
static: f x 

Env level 2
static: 

Env level 3
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 3 variables.

```


index with + goes up the stack relative to current (1 in this case):
```nix
:st +3
```
```output

4: while evaluating a 'let' expression
«string»:1:1

     1| let f = _: throw "x_x"; x = f 5; in x
      | ^

Env level 0
static: f x 

Env level 1
static: 

Env level 2
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 2 variables.

```



index with - goes down and is also relative to current(4):

```nix
:st -1
```
```output

3: while calling a function
«string»:1:29

     1| let f = _: throw "x_x"; x = f 5; in x
      |                             ^

Env level 0
static: f x 

Env level 1
static: 

Env level 2
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 2 variables.

```


quit
```nix
:quit
```
```output
error: x_x

```
