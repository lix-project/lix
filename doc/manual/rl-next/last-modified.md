---
synopsis: "Fix handling of `lastModified` in tarball inputs"
issues: []
cls: [2792]
category: Fixes
credits: [xanderio, blitz]
---

Previous versions of Lix would fail with the following error, if a
[tarball flake input](@docroot@/protocols/tarball-fetcher.md) redirect
to a URL that contains a `lastModified` field:

```
error: input attribute 'lastModified' is not an integer
```

This is now fixed.
