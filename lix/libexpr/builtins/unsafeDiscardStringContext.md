---
name: unsafeDiscardStringContext
args: [s]
---

Returns a copy of the string `s` with all string context associated with `s` removed.
Since string context is used for dependency tracking the returned string will also have
*no dependencies* on store objects, even when the original string `s` had such dependencies.

This function is mainly useful when discarding dependencies is explicitly required, e.g.
to produce a string listing all inputs of a derivation without propagating these inputs
as dependencies into all *users* of the listing. For example, the derivation `dep` in the
following example will pull `hello` into its closure despite never using it while `nodep`
will not:

```nix
dep = runCommand "dep" {
  inherit hello;
} "echo hello is at $hello >$out";

nodep = runCommand "nodep" {
  hello = builtins.unsafeDiscardStringContext hello;
} "echo hello is at $hello >$out";
```

This behavior also makes this function unsafe: if `s` contains the path of a
store object that is not present in the store then any use of `s` in a
derivation tree will attempt to realize that path in the store, but no use
of `unsafeDiscardStringContext s` will. This can lead to derivation outputs that
refer to paths that were never created.

Lix cannot determine whether such reference are safe or not and must pass
this obligation to the user.
