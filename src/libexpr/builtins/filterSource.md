---
name: filterSource
args: [e1, e2]
---
> **Warning**
>
> `filterSource` should not be used to filter store paths. Since
> `filterSource` uses the name of the input directory while naming
> the output directory, doing so will produce a directory name in
> the form of `<hash2>-<hash>-<name>`, where `<hash>-<name>` is
> the name of the input directory. Since `<hash>` depends on the
> unfiltered directory, the name of the output directory will
> indirectly depend on files that are filtered out by the
> function. This will trigger a rebuild even when a filtered out
> file is changed. Use `builtins.path` instead, which allows
> specifying the name of the output directory.

This function allows you to copy sources into the Nix store while
filtering certain files. For instance, suppose that you want to use
the directory `source-dir` as an input to a Nix expression, e.g.

```nix
stdenv.mkDerivation {
  ...
  src = ./source-dir;
}
```

However, if `source-dir` is a Subversion working copy, then all
those annoying `.svn` subdirectories will also be copied to the
store. Worse, the contents of those directories may change a lot,
causing lots of spurious rebuilds. With `filterSource` you can
filter out the `.svn` directories:

```nix
src = builtins.filterSource
  (path: type: type != "directory" || baseNameOf path != ".svn")
  ./source-dir;
```

Thus, the first argument *e1* must be a predicate function that is
called for each regular file, directory or symlink in the source
tree *e2*. If the function returns `true`, the file is copied to the
Nix store, otherwise it is omitted. The function is called with two
arguments. The first is the full path of the file. The second is a
string that identifies the type of the file, which is either
`"regular"`, `"directory"`, `"symlink"` or `"unknown"` (for other
kinds of files such as device nodes or fifos — but note that those
cannot be copied to the Nix store, so if the predicate returns
`true` for them, the copy will fail). If you exclude a directory,
the entire corresponding subtree of *e2* will be excluded.
