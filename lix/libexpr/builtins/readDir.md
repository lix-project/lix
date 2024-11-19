---
name: readDir
args: [path]
---
Return the contents of the directory *path* as a set mapping
directory entries to the corresponding file type. For instance, if
directory `A` contains a regular file `B` and another directory
`C`, then `builtins.readDir ./A` will return the set

```nix
{ B = "regular"; C = "directory"; }
```

The possible values for the file type are `"regular"`,
`"directory"`, `"symlink"` and `"unknown"`.
