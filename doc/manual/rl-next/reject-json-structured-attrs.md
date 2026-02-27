---
synopsis: "Reject `__json` in structured attributes derivations"
cls: [5286]
issues: [fj#380]
category: "Improvements"
credits: [raito]
---

In structured attributes derivations, `__json` is used internally to store the
JSON representation of the `env` attribute field that users can set.

Unfortunately, a user can set `__json` *and* enable structured attributes,
resulting in a broken derivation from a semantic point of view.

As no user can benefit from setting `__json` *and* enable structured attributes,
we disallow that possibility and throw an error from now on.

This is not seen as a breaking change because there's no user code that can
benefit from this behavior, hence, it's an improvement to user experience.
