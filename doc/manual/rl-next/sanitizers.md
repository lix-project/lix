---
synopsis: "Lix now supports building with UndefinedBehaviorSanitizer"
cls: [1483]
credits: [jade]
category: Development
---

You can now build Lix with the configuration option `-Db_sanitize=undefined` and it will both work and pass tests. AddressSanitizer support is also coming soon.

For a list of undefined behaviour fixed by sanitizer usage, see [the gerrit topic "undefined-behaviour"](https://gerrit.lix.systems/q/topic:%22undefined-behaviour%22).
