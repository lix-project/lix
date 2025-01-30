---
synopsis: 'Allow specifying ports for remote ssh[-ng] stores'
issues: []
cls: [2432]
category: Improvements
credits: [seppel3210]
---

You can now specify which port should be used for a remote ssh store (e.g. for remote/distributed builds) through a uri parameter.
E.g., when a remote builder `foo` is listening on port `1234` instead of the default, it can be specified like this `ssh://foo?port=1234`.
