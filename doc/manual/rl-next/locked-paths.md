---
synopsis: Don't consider a path with a specified rev to be `locked`
issues: []
cls: [2064]
category: Fixes
credits: [ma27]
---

Until now it was allowed to do e.g.

    $ echo 'lalala' > testfile
    $ nix eval --expr '(builtins.fetchTree { path = "/home/ma27/testfile"; rev = "0000000000000000000000000000000000000000"; type = "path"; })'
    { lastModified = 1723656303; lastModifiedDate = "20240814172503"; narHash = "sha256-hOMY06A0ohaaCLwnhpZIMoAqi/8kG2vk30NRiqi0dfc="; outPath = "/nix/store/lhfz259iipmv9ky995rml8018jvriynh-source"; rev = "0000000000000000000000000000000000000000"; shortRev = "0000000"; }
    $ cat /nix/store/lhfz259iipmv9ky995rml8018jvriynh-source
    lalala

because any kind of input with a `rev` specified is considered to be locked.

With this change, inputs of type `path`, `indirect` and `tarball` are no longer
considered locked with a rev, but no hash specified.

This behavior was changed in
[CppNix 2.21 as well](https://github.com/nixos/nix/commit/071dd2b3a4e6c0b2106f1b6f14ec26e153d97446) as well.
