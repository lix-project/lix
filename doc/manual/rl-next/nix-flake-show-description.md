---
synopsis: "Lix will now show the package descriptions in when running `nix flake show`."
cls: [1540]
issues: []
credits: [kjeremy, isabelroses]
category: Improvements
---

When running `nix flake show`, Lix will now show the package descriptions, if they exist.

Before:

```shell
$ nix flake show
path:/home/isabel/dev/lix-show?lastModified=1721736108&narHash=sha256-Zo8HP1ur7Q2b39hKUEG8EAh/opgq8xJ2jvwQ/htwO4Q%3D
└───packages
    └───x86_64-linux
        ├───aNoDescription: package 'simple'
        ├───bOneLineDescription: package 'simple'
        ├───cMultiLineDescription: package 'simple'
        ├───dLongDescription: package 'simple'
        └───eEmptyDescription: package 'simple'
```

After:

```shell
$ nix flake show
path:/home/isabel/dev/lix-show?lastModified=1721736108&narHash=sha256-Zo8HP1ur7Q2b39hKUEG8EAh/opgq8xJ2jvwQ/htwO4Q%3D
└───packages
    └───x86_64-linux
        ├───aNoDescription: package 'simple'
        ├───bOneLineDescription: package 'simple' - 'one line'
        ├───cMultiLineDescription: package 'simple' - 'line one'
        ├───dLongDescription: package 'simple' - 'abcdefghijklmnopqrstuvwxyz'
        └───eEmptyDescription: package 'simple'
```
