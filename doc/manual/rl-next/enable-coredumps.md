---
synopsis: "Add an option `enable-core-dumps` that enables core dumps from builds"
cls: 1088
credits: midnightveil
category: Features
---

In the past, Lix disabled core dumps by setting the soft `RLIMIT_CORE` to 0
unconditionally. Although this rlimit could be altered from the builder since
it is just the soft limit, this was kind of annoying to do. By passing
`--option enable-core-dumps true` to an offending build, one can now cause the
core dumps to be handled by the system in the normal way (winding up in
`coredumpctl`, say, on Linux).
