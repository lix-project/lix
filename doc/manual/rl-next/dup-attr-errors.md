---
synopsis: Duplicate attribute reports are more accurate
# prs: cl 557
---

Duplicate attribute errors are now more accurate, showing the path at which an error was detected rather than the full, possibly longer, path that caused the error.
Error reports are now
```ShellSession
$ nix eval --expr '{ a.b = 1; a.b.c.d = 1; }'
error: attribute 'a.b' already defined at «string»:1:3
       at «string»:1:12:
            1| { a.b = 1; a.b.c.d = 1;
             |            ^
```
instead of
```ShellSession
$ nix eval --expr '{ a.b = 1; a.b.c.d = 1; }'
error: attribute 'a.b.c.d' already defined at «string»:1:3
       at «string»:1:12:
            1| { a.b = 1; a.b.c.d = 1;
             |            ^
```
