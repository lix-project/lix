---
synopsis: "Trace which part of a `foo.bar.baz` expression errors"
cls: [1505, 1506]
credits: Qyriad
category: Improvements
---

Previously, if an attribute path selection expression like `linux_4_9.meta.description` it wouldn't show you which one of those parts in the attribute path, or even that that line of code is what caused evaluation of the failing expression.
The previous error looks like this:

```
pkgs.linuxKernel.kernels.linux_4_9.meta.description

error:
       … while evaluating the attribute 'linuxKernel.kernels.linux_4_9.meta.description'
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |     ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       … while calling the 'throw' builtin
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |                 ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       error: linux 4.9 was removed because it will reach its end of life within 22.11
```

Now, the error will look like this:

```
pkgs.linuxKernel.kernels.linux_4_9.meta.description

error:
       … while evaluating the attribute 'linuxKernel.kernels.linux_4_9.meta.description'
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |     ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       … while evaluating 'pkgs.linuxKernel.kernels.linux_4_9' to select 'meta' on it
         at «string»:1:1:
            1| pkgs.linuxKernel.kernels.linux_4_9.meta.description
             | ^

       … caused by explicit throw
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |                 ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       error: linux 4.9 was removed because it will reach its end of life within 22.11
```

Not only does the line of code that referenced the failing attribute show up in the trace, it also tells you that it was specifically the `linux_4_9` part that failed.

This includes if the failing part is a top-level binding:

```
let
  inherit (pkgs.linuxKernel.kernels) linux_4_9;
in linux_4_9.meta.description
error:
       … while evaluating 'linux_4_9' to select 'meta.description' on it
         at «string»:3:4:
            2|   inherit (pkgs.linuxKernel.kernels) linux_4_9;
            3| in linux_4_9.meta.description
             |    ^

       … while evaluating the attribute 'linux_4_9'
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |     ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       … caused by explicit throw
         at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
          277|   } // lib.optionalAttrs config.allowAliases {
          278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
             |                 ^
          279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

       error: linux 4.9 was removed because it will reach its end of life within 22.11
```
