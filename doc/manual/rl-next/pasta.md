---
synopsis: "Fixed output derivations can be run using `pasta` network isolation"
cls: [3452]
issues: [fj#285]
category: "Breaking Changes"
credits: [horrors, puck]
---

Fixed output derivations traditionally run in the host network namespace.
On Linux this allows such derivations to communicate with other sandboxes
or the host using the abstract Unix domains socket namespace; this hasn't
been unproblematic in the past and has been used in two distinct exploits
to break out of the sandbox. For this reason fixed output derivations can
now run in a network namespace (provided by [`pasta`]), restricted to TCP
and UDP communication with the rest of the world. When enabled this could
be a breaking change and we classify it as such, even though we don't yet
enable or require such isolation by default. We may enforce this in later
releases of Lix once we have sufficient confidence that breakage is rare.

[`pasta`]: https://passt.top/
