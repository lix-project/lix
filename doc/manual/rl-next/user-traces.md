---
synopsis: "Always print frames from `addErrorContext` in error traces"
cls: [5847]
category: "Improvements"
credits: [blokyk]
issues: []
---

The [`builtins.addErrorContext`](@docroot@/language/builtins.md#builtins-addErrorContext)
function allows an author to add artificial stack frames with custom messages to
help end-users understand the context of an error and the path the code took to
get there, without having to read and understand the original source code. A
particularly notable user of this is the Nixpkgs module system, which adds
custom frames detailing what option it's evaluating or which definition it's
looking at.

However, previously, these frames would end up treated just as any other,
meaning they would most often not be visible without `--show-trace`; yet, using
`--show-trace`, they would be drowned out in the noise of the hundreds of other
frames, rendering them just as unusable.

With this change, these frames are now unconditionally shown, even without
`--show-trace`, which makes basic error traces much more informative.
