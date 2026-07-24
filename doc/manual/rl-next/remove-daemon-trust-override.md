---
synopsis: "Remove the `daemon-trust-override` experimental feature"
cls: [6071]
category: Miscellany
credits: [horrors]
---

The `daemon-trust-override` experimental feature has been removed. It provided no large benefit over
custom configuration using e.g. the `NIX_CONFIG` enviroment variable and was only used in our tests.
