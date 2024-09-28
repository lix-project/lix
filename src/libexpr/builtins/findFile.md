---
name: findFile
args: ['search path', 'lookup path']
---
Look up the given path with the given search path.

A search path is represented list of [attribute sets](./values.md#attribute-set) with two attributes, `prefix`, and `path`.
`prefix` is a relative path.
`path` denotes a file system location; the exact syntax depends on the command line interface.

Examples of search path attribute sets:

- ```
  {
    prefix = "nixos-config";
    path = "/etc/nixos/configuration.nix";
  }
  ```

- ```
  {
    prefix = "";
    path = "/nix/var/nix/profiles/per-user/root/channels";
  }
  ```

The lookup algorithm checks each entry until a match is found, returning a [path value](@docroot@/language/values.html#type-path) of the match.

This is the process for each entry:
If the lookup path matches `prefix`, then the remainder of the lookup path (the "suffix") is searched for within the directory denoted by `patch`.
Note that the `path` may need to be downloaded at this point to look inside.
If the suffix is found inside that directory, then the entry is a match;
the combined absolute path of the directory (now downloaded if need be) and the suffix is returned.

The syntax

```nix
<nixpkgs>
```

is equivalent to:

```nix
builtins.findFile builtins.nixPath "nixpkgs"
```
