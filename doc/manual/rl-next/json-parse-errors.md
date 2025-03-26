---
synopsis: "Fix Lix crashing on invalid json"
issues: [fj#642, fj#753, fj#759, fj#769]
cls: [2907]
category: "Fixes"
credits: ["horrors"]
---

Lix no longer crashes when it receives invalid JSON. Instead it'll point to the syntax error and give some context about what happened, for example

```
❯ nix derivation add <<<"""
error:
       … while parsing a derivation from stdin

       error: failed to parse JSON: [json.exception.parse_error.101] parse error at line 2, column 1: syntax error while parsing value - unexpected end of input; expected '[', '{', or a literal
```
