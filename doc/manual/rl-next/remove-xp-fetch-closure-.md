---
synopsis: Remove `fetch-closure` experimental feature
issues: [fj#1010]
cls: [4595]
category: "Breaking Changes"
credits: [just1602]
---

The `fetch-closure` experimental feature has been removed.

Outside of allowing the user to import closure from binary cache,
`fetchClosure` also allow you to do the following:

* rewrite non-CA path to CA
* reject non-CA paths at fetching time
* reject CA paths at fetching time

Some people are using those mechanism to prevent users from having to build any
package and force going via the declared cache or as a way to use ancient/old
software without paying the evaluation cost of a second nixpkgs.

Both use cases are somewhat of an antipattern in Nix semantics. If the user
cannot fetch a program directly via the substituter mechanism and fall back to
local build, this is a feature AND a misconfiguration. If the user cannot build
certain derivations because they are too expensive, the build directives should
pass `-j0` or similar.

As for the second usecase, there's a different way to do it that also allows to
have a way to reproduce the paths that are hardcoded in that file, perform
`import (fetchurl "https://my-cache/${hashparts storepath}.drv")` rather, i.e.
an IFD to a possibly well known name. The backend can generate them on the fly
or once, and possess stable names.

Finally, as for the non-CA → CA features, Lix removed ca-derivations.
fetchClosure offers ca-derivations-like features which suffers from similar
shortcomings albeit lessened. It only follows that we should rather deprecate
and remove these capabilities.
