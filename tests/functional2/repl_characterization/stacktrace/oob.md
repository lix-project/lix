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



frames from 0 up to 4 work fine
```nix
:st 0
:st 4
```
```output

0: error: x_x
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


absolute frames out of bounds print an error
```nix
:st 5
```
```output
error: stack index must be between 0 and 4 (inclusive), but was 5

```


argument-less `:st` is still the same afer absolute oob
```nix
:st
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


positive relative frames oob clamp to upper bound and prints a warning
```nix
:st +5
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
warning: stopped at stack frame 4, cannot go any higher

```


negative relative frames oob clamp to lower bound and print a warning
```nix
:st -5
```
```output

0: error: x_x
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
warning: stopped at stack frame 0, cannot go any deeper

```



quit
```nix
:quit
```
```output
error: x_x

```
