---
synopsis: "Fetch peer PID for daemon connections on macOS"
issues: [fj#640]
cls: [2453]
category: Fixes
credits: [lilyball]
---

`nix-daemon` will now fetch the peer PID for connections on macOS, to match behavior with Linux.
Besides showing up in the log output line, If `nix-daemon` is given an argument (such as `--daemon`)
that argument will be overwritten with the peer PID for the forked process that handles the connection,
which can be used for debugging purposes.
