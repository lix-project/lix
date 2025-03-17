---
synopsis: "Parsing failures in flake.lock no longer crash Lix"
issues: [fj#559]
cls: [2401]
category: "Fixes"
credits: ["gilice"]
---
Failure to parse `flake.lock` no longer hard-crashes Lix and instead produces a nice error message.

```
error:
       … while updating the lock file of flake 'git+file:///Users/jade/lix/lix2'

       … while parsing the lock file at /nix/store/mm5dqh8a729yazzj82cjffxl97n5c62s-source//flake.lock

       error: [json.exception.parse_error.101] parse error at line 1, column 1: syntax error while parsing value - invalid literal;
 last read: '#'
```
