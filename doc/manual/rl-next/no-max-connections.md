---
synopsis: "Remove `max-connections` store parameters for `ssh://` and `ssh-ng://` stores"
cls: []
category: Miscellany
credits: [horrors]
---

The `max-connections` parameter was undocumented, untested, and (in the case of `ssh`) even ignored
entirely for remote builds. During a survey of public nixos configurations we have found *two* uses
of `max-connections` for `ssh-ng`, and none at all for `ssh`. Since it is so rarely used but brings
significant internal complexity that hinders improvements we have decided to remove these features.
