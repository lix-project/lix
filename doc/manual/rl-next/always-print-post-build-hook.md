---
synopsis: "Always print `post-build-hook` logs"
issues: [fj#675]
cls: [2801]
category: "Improvements"
credits: ["jade"]
---

Logs of `post-build-hook` are now printed unconditionally.
They used to be tied to whether print-build-logs is set, which made debugging them a nightmare when they fail, since the failure output would be eaten if build logs are disabled.
Most usages of `post-build-hook` are pretty quiet especially compared to build logs, so it should not be that bothersome to not be able to turn off.
