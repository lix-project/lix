---
synopsis: "REPL improvements"
issues: []
cls: [2319, 2320, 2321]
category: "Improvements"
credits: ["piegames"]
---

The REPL has seen various minor improvements:

- Variable declarations have been improved, making copy-pasting code from attrsets a lot easier:
  - Declarations can now optionally end with a semicolon
  - Multiple declarations can be done within one command, separated by semicolon
  - The `foo.bar = "baz";` syntax from attrsets is also supported, however without the attrset merging rules and with restrictions on dynamic attrs like in `let` bindings.
  - Variable names now use the proper Nix grammar rules, instead of a regex that only vaguely matched legal identifiers.
- Better error messages overall
- The `:env` command to print currently available variables now also works outside of debug mode
- Adding variables to the REPL now prints a small message on success
