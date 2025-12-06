---
synopsis: "`--check` or `--rebuild` is clearer about a missing path"
cls: []
issues: [fj#485]
category: "Improvements"
credits: [raito]
---

Previously, when running Lix with --check or --rebuild, failures often surfaced
as an unhelpful error:

> "some outputs of '...' are not valid, so checking is not possible"

This message could mean two different things:

- The requested output paths don't exist at all, or,
- Some outputs exist but are not known to Lix

Lix cannot reliably distinguish these cases, so it treated them the same.

We've updated the error messages to clarify what Lix can determine: whether any
valid outputs (> 0) are present or whether no outputs are available.

When no valid outputs can be found, Lix will now suggest building the derivation
normally (without --check or --rebuild) before trying again.

When some valid outputs are present, Lix now reports which ones are valid,
shows the full list of known outputs, and also suggests building the derivation
normally.

In the future, Lix may automate this recovery step when it knows how to rebuild
the paths, but implementing that safely requires more extensive changes to the
codebase.
