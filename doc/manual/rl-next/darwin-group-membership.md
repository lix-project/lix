---
synopsis: "Test group membership better on macOS"
issues: [gh#5885]
cls: [2566]
category: Fixes
credits: [lilyball]
---

`nix-daemon` will now test group membership better on macOS for `trusted-users` and `allowed-users`.
It not only fetches the peer gid (which fixes `@staff`) but it also asks opendirectory for group
membership checks instead of just using the group database, which means nested groups (like `@_developer`)
and groups with synthesized membership (like `@localaccounts`) will work.
