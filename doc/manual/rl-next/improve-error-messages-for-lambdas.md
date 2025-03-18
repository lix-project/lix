---
synopsis: 'Show all missing and unexpected arguments in erroneous function calls'
issues: []
cls: [2477]
category: Improvements
credits: [quantenzitrone]
---

When calling a function that expects an attribute set, lix will now show all
missing and unexpected arguments.
e.g. with `({ a, b, c } : a + b + c) { a = 1; d = 1; }` lix will now show the error:
```
[...]
error: function 'anonymous lambda' called without required arguments 'b' and 'c' and with unexpected argument 'd'
[...]
```
Previously lix would just show `b`.
Furthermore lix will now only suggest arguments that aren't yet used.
e.g. with `({ a?1, b?1, c?1 } : a + b + c) { a = 1; d = 1; e = 1; }` lix will now show the error:
```
[...]
error: function 'anonymous lambda' called with unexpected arguments 'd' and 'e'
       at «string»:1:2:
            1| ({ a?1, b?1, c?1 } : a + b + c) { a = 1; d = 1; e = 1; }
             |  ^
       Did you mean one of b or c?
```
Previously lix would also suggest `a`.
Suggestions are unfortunately still currently just for the first missing argument.
