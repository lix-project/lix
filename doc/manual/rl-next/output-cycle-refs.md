---
synopsis: "show tree with references that lead to an output cycle"
issues: [fj#551]
category: Improvements
credits: [ma27]
---

When Lix determines a cyclic dependency between several outputs of a derivation,
it now displays which files in which outputs lead to an output cycle:

```
error: cycle detected in build of '/nix/store/gc5h2whz3rylpf34n99nswvqgkjkigmy-demo.drv' in the references of output 'bar' from output 'foo'.

       Shown below are the files inside the outputs leading to the cycle:
       /nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar
       └───lib/libfoo: …stuffbefore /nix/store/h680k7k53rjl9p15g6h7kpym33250w0y-demo-baz andafter.…
           → /nix/store/h680k7k53rjl9p15g6h7kpym33250w0y-demo-baz
           └───share/snenskek: …???? /nix/store/dm24c76p9y2mrvmwgpmi64rryw6x5qmm-demo-foo ....…
               → /nix/store/dm24c76p9y2mrvmwgpmi64rryw6x5qmm-demo-foo
               └───bin/alarm: …textexttext/nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar abcabcabc.…
                   → /nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar
```

Please note that showing the files and its contents while displaying the cycles only works
on Linux.
