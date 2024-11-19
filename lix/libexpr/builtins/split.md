---
name: split
args: [regex, str]
---
Returns a list composed of non matched strings interleaved with the
lists of the [extended POSIX regular
expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
*regex* matches of *str*. Each item in the lists of matched
sequences is a regex group.

```nix
builtins.split "(a)b" "abc"
```

Evaluates to `[ "" [ "a" ] "c" ]`.

```nix
builtins.split "([ac])" "abc"
```

Evaluates to `[ "" [ "a" ] "b" [ "c" ] "" ]`.

```nix
builtins.split "(a)|(c)" "abc"
```

Evaluates to `[ "" [ "a" null ] "b" [ null "c" ] "" ]`.

```nix
builtins.split "([[:upper:]]+)" " FOO "
```

Evaluates to `[ " " [ "FOO" ] " " ]`.
