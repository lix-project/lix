---
args: ['--debugger']
---

```nix
throw "(forever????????)"
```
```output
error: (forever????????)

```


argument-less :st works fine

```nix
:st
```
```output

0: error: (forever????????)
«string»:1:1

     1| throw "(forever????????)"
      | ^

Env level 0
static: 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

```


a non-numeric string produces an error

```nix
:st chat
:st bedroom community
```
```output
error: argument 'chat' is not a valid integer

error: argument 'bedroom community' is not a valid integer

```


even when they start with a digit
```nix
:st 6up
```
```output
error: argument '6up' is not a valid integer

```


or when they're floats
```nix
:st 4.50
```
```output
error: argument '4.50' is not a valid integer

```



an integer outside of the  range produces an error
```nix
:st 23571113171923
```
```output
error: argument '23571113171923' is not a valid integer

```


argument-less `:st` is still at the same index after errors
```nix
:st 1
:st foo
:st
```
```output

1: while calling a function
«string»:1:1

     1| throw "(forever????????)"
      | ^

Env level 0
static: 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 


error: argument 'foo' is not a valid integer


1: while calling a function
«string»:1:1

     1| throw "(forever????????)"
      | ^

Env level 0
static: 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

```


quit
```nix
:quit
```
```output
error: (forever????????)

```
