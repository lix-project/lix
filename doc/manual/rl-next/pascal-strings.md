---
synopsis: "Strings may now contain NUL bytes"
cls: [3968]
issues: []
category: "Breaking Changes"
credits: [horrors]
---

Lix now allows strings to contain NUL bytes instead of silently truncating the
string before the first such byte. Notably NUL-bearing strings were allowed as
attribute names—even though the corresponding strings were not representable!—
leading to very surprising and incorrect behavior in corner cases, for example

```
nix-repl> builtins.fromJSON ''{"a": 1, "a\u0000b": 2}''
{
  a = 1;
  "ab" = 2;
}

nix-repl> builtins.attrNames (builtins.fromJSON ''{"a": 1, "a\u0000b": 2}'')
[
  "a"
  "a"
]
```

rather than the more correct but still with the terminal eating NUL on display

```
nix-repl> builtins.fromJSON ''{"a": 1, "a\u0000b": 2}''
{
  a = 1;
  "ab" = 2;
}

nix-repl> builtins.attrNames (builtins.fromJSON ''{"a": 1, "a\u0000b": 2}'')
[
  "a"
  "ab"
]
```

We consider this a breaking change since eval results *will* change if strings
with embedded NUL bytes were used, but we also consider the old behavior to be
not intentional (seeing how inconsistent it was) but merely fallout from a old
and misguided implementation decision to be worked around, not actually fixed.
