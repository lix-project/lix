---
synopsis: "Some Lix crashes now produce reporting instructions and a stack trace, then abort"
cls: [1854]
category: Improvements
credits: jade
---

Lix, being a C++ program, can crash in a few kinds of ways.
It can obviously do a memory access violation, which will generate a core dump and thus be relatively debuggable.
But, worse, it could throw an unhandled exception, and, in the past, we would just show the message but not where it comes from, in spite of this always being a bug, since we expect all such errors to be translated to a Lix specific error.
Now the latter kind of bug should print reporting instructions, a rudimentary stack trace and (depending on system configuration) generate a core dump.

Sample output:

```
Lix crashed. This is a bug. We would appreciate if you report it along with what caused it at https://git.lix.systems/lix-project/lix/issues with the following information included:

Exception: std::runtime_error: test exception
Stack trace:
 0# nix::printStackTrace() in /home/jade/lix/lix3/build/src/nix/../libutil/liblixutil.so
 1# 0x000073C9862331F2 in /home/jade/lix/lix3/build/src/nix/../libmain/liblixmain.so
 2# 0x000073C985F2E21A in /nix/store/p44qan69linp3ii0xrviypsw2j4qdcp2-gcc-13.2.0-lib/lib/libstdc++.so.6
 3# 0x000073C985F2E285 in /nix/store/p44qan69linp3ii0xrviypsw2j4qdcp2-gcc-13.2.0-lib/lib/libstdc++.so.6
 4# nix::handleExceptions(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, std::function<void ()>) in /home/jade/lix/lix3/build/src/nix/../libmain/liblixmain.so
 ...
```
