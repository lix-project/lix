---
synopsis: "Fix unexpectedly-successful GC failures on macOS"
cls: 1723
issues: fj#446
credits: jade
category: Fixes
---

Has the following happened to you on macOS? This failure has been successfully eliminated, thanks to our successful deployment of advanced successful-failure detection technology (it's just `if (failed && errno == 0)`. Patent pending<sup>not really</sup>):

```
$ nix-store --gc --print-dead
finding garbage collector roots...
error: Listing pid 87261 file descriptors: Undefined error: 0
```
