---
synopsis: "Includes are now qualified with library name"
category: Development
cls: [2178]
credits: jade
---

The Lix includes have all been rearranged to be of the form `"lix/libexpr/foo.hh"` instead of `"foo.hh"`.
This was already supported externally for a migration period, but it is now being applied to all the internal usages within Lix itself.
The goal of this change is to both clarify where a file is from and to avoid polluting global include paths with things like `config.h` that might conflict with other projects.

For other details, see the release notes of Lix 2.90.0, under "Rename all the libraries" in Breaking Changes.

To fix an external project with sources in `src` which has a separate build directory (such that headers are in `../src` relative to where the compiler is running):

```
lix_root=$HOME/lix
(cd $lix_root && nix develop -c 'meson setup build && ninja -C build subprojects/lix-clang-tidy/liblix-clang-tidy.so')
run-clang-tidy -checks='-*,lix-fixincludes' -load=$lix_root/build/subprojects/lix-clang-tidy/liblix-clang-tidy.so -p build/ -header-filter '\.\./src/.*\.h' -fix src
```
