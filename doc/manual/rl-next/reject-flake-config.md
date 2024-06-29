---
synopsis: Allow automatic rejection of configuration options from flakes
cls: [1541]
credits: [alois31]
category: Improvements
---

Setting `accept-flake-config` to `false` now respects user choice by automatically rejecting configuration options set by flakes.
The old behaviour of asking each time is still available (and default) by setting it to the special value `ask`.
