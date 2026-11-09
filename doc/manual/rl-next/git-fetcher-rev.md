---
synopsis: "Fetching explicit Git revisions no longer depends on the default branch"
issues: []
cls: [3119]
category: "Fixes"
credits: [raito, winter]
---

`builtins.fetchGit` and `fetchTree` now fetch an explicitly requested `rev`
directly instead of first fetching the default branch and then looking for the
revision, as long as no explicit `ref` is supplied.

This makes revs that live on tags or release branches, but are not reachable
from the remote's default branch, work without setting `allRefs = true`.

When a `ref` is supplied, it is fetched as before and the `rev` must be found
in it. `rev` does not take precedence over `ref`. This preserves the Nix 2.3
behaviour of accepting a commit hash as `ref`, including when a matching `rev`
is also supplied.

When the revision is fetched directly, the fetcher no longer creates a
`refs/heads/<rev>` entry in its Git cache.
