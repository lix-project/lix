---
synopsis: Clang build timing analysis
cls: 587
---

We now have Clang build profiling available, which generates Chrome
tracing files for each compilation unit. To enable it, run `meson configure
build -Dprofile-build=enabled` then rerun the compilation.

If you want to make the build go faster, do a clang build with meson, then run
`maintainers/buildtime_report.sh build`, then contemplate how to improve the
build time.

You can also look at individual object files' traces in
<https://ui.perfetto.dev>.
