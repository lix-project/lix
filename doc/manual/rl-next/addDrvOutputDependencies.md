---
synopsis: "Add a builtin `addDrvOutputDependencies`"
prs: 9216
issues: 7910
credits: [ericson, horrors]
category: Features
---

This builtin allows taking a `drvPath`-like string and turning it into a string
with context such that, when it lands in a derivation, it will create
dependencies on *all the outputs* in its closure (!). Although `drvPath` does this
today, this builtin starts forming a path to migrate to making `drvPath` have a
more normal and less surprising string context behaviour (see linked issue and
PR for more details).
