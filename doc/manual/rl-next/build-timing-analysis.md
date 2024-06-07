---
synopsis: Clang build timing analysis
cls: 587
category: Development
---

We now have Clang build profiling available, which generates Chrome
tracing files for each compilation unit. To enable it, run `meson configure
build -Dprofile-build=enabled` in a Clang stdenv (`nix develop
.#native-clangStdenvPackages`) then rerun the compilation.

If you want to make the build go faster, do a clang build with meson, then run
`maintainers/buildtime_report.sh build`, then contemplate how to improve the
build time.

You can also look at individual object files' traces in
<https://ui.perfetto.dev>.

See [the wiki page][improving-build-times-wiki] for more details on how to do
this.

[improving-build-times-wiki]: https://wiki.lix.systems/link/8#bkmrk-page-title
