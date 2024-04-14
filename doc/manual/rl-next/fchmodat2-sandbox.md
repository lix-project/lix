---
synopsis: Creating setuid/setgid binaries with fchmodat2 is now prohibited by the build sandbox
prs: 10501
---

The build sandbox blocks any attempt to create setuid/setgid binaries, but didn't check
for the use of the `fchmodat2` syscall which was introduced in Linux 6.6 and is used by
glibc >=2.39. This is fixed now.
