---
synopsis: Rename all the libraries nixexpr, nixstore, etc to lixexpr, lixstore, etc
credits: jade
category: Breaking Changes
---

The Lix C++ API libraries have had the following changes:
- Includes moved from `include/nix/` to `include/lix/`
- `pkg-config` files renamed from `nix-expr` to `lix-expr` and so on.
- Libraries renamed from `libnixexpr.so` to `liblixexpr.so` and so on.

There are other changes between Nix 2.18 and Lix, since these APIs are not
stable. However, this change in particular is a deliberate compatibility break
to force downstreams linking to Lix to specifically handle Lix and avoid Lix
accidentally getting ensnared in compatibility code for newer CppNix.

Migration path:

- expr.hh      -> lix/libexpr/expr.hh
- nix/config.h -> lix/config.h

To apply this migration automatically, remove all `<nix/>` from includes, so `#include <nix/expr.hh>` -> `#include <expr.hh>`.
Then, the correct paths will be resolved from the tangled mess, and the clang-tidy automated fix will work.

Then run the following for out of tree projects:

```console
lix_root=$HOME/lix
(cd $lix_root/clang-tidy && nix develop -c 'meson setup build && ninja -C build')
run-clang-tidy -checks='-*,lix-fixincludes' -load=$lix_root/clang-tidy/build/liblix-clang-tidy.so -p build/ -fix src
```
