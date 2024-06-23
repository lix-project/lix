---
synopsis: "Trace when the `foo` part of a `foo.bar.baz` expression errors"
cls: 1505
credits: Qyriad
category: Improvements
---

Previously, if an expression like `linux_4_9.meta.description` errored in the `linux_4_9` part, it wouldn't show you that that's the part of the expression that failed to evaluate, or even that that line of code is what caused evaluation of the failing expression.
The previous error looks like this:

```
let
  inherit (pkgs.linuxKernel.kernels) linux_4_9;
in linux_4_9.meta.description

error:
       … while evaluating the attribute 'linux_4_9'
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

Not only does the line of code that referenced the failing binding show up in the trace, it also tells you that it was specifically the `linux_4_9` part that failed.
