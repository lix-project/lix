---
synopsis: "Lix now supports building with UndefinedBehaviorSanitizer"
cls: [1483, 1481, 1669]
credits: [jade]
category: Development
---

You can now build Lix with the configuration option `-Db_sanitize=undefined,address` and it will both work and pass tests with both AddressSanitizer and UndefinedBehaviorSanitizer enabled.
To use ASan specifically, you have to set `-Dgc=disabled`, which an error message will tell you to do if necessary anyhow.

Furthermore, tests passing with Clang ASan+UBSan is checked on every change in CI.

For a list of undefined behaviour found by tooling usage, see [the gerrit topic "undefined-behaviour"](https://gerrit.lix.systems/q/topic:%22undefined-behaviour%22).
