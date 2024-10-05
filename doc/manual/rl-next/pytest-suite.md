---
synopsis: "The beginnings of a new pytest-based functional test suite"
category: Development
cls: [2036, 2037]
credits: jade
---

The existing integration/functional test suite is based on a large volume of shell scripts.
This often makes it somewhat challenging to debug at the best of times.
The goal of the pytest test suite is to make tests have more obvious dependencies on files and to make tests more concise and easier to write, as well as making new testing methods like snapshot testing easy.
