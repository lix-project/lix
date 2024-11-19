---
synopsis: All Lix threads are named
cls: [2210]
category: Development
credits: [jade]
---

Lix now sets thread names on all of its secondary threads, which will make debugger usage slightly nicer and easier.

```
(gdb) info thr
  Id   Target Id                    Frame
* 1    LWP 3719283 "nix-daemon"     0x00007e558587da0f in accept ()
   from target:/nix/store/c10zhkbp6jmyh0xc5kd123ga8yy2p4hk-glibc-2.39-52/lib/libc.so.6
  2    LWP 3719284 "signal handler" 0x00007e55857b2bea in sigtimedwait ()
   from target:/nix/store/c10zhkbp6jmyh0xc5kd123ga8yy2p4hk-glibc-2.39-52/lib/libc.so.6
```
