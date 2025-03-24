---
synopsis: "Add support for eBPF USDT/dtrace probes inside Lix"
issues: [fj#727]
cls: [2884]
category: Features
credits: jade
---

eBPF tracers like `bpftrace` and `dtrace` are a group of similar tools for debugging production systems.
User-space statically defined tracing probes (USDT) allow for defining zero or near-zero disabled-probe-effect probes, thus allowing instrumentation of hot paths in production builds.
Lix now has internal support for defining these probes and has shipped its first probe.

As of this writing it is available by default in the Linux build of Lix.

To try it out on Linux, you can use the following example command:

```
$ sudo bpftrace -l 'usdt:/path/to/liblixstore.so:*:*'
usdt:/path/to/liblixstore.so:lix_store:filetransfer__read

$ sudo bpftrace -e 'usdt:*:lix_store:filetransfer__read { printf("%s read %d\n", str(arg0), arg1); }'
Attaching 1 probe...
https://cache.nixos.org/wvpzaycmvs39h5bcsfrxkjsg48mj4h73.narinf.. read 8192
https://cache.nixos.org/wvpzaycmvs39h5bcsfrxkjsg48mj4h73.narinf.. read 8192
https://cache.nixos.org/nar/1qshsc30nlarzdig0v9b1aasdkwaxhnv0a0.. read 65536
https://cache.nixos.org/nar/1qshsc30nlarzdig0v9b1aasdkwaxhnv0a0.. read 65536
```

Note that bpftrace does not offer any way to list the arguments to USDT probes in a human readable form.
To get the probe definitions, see the `*.d` files in the Lix source code, for example, `lix/libstore/trace-probes.d`.

For more resources on eBPF/bpftrace and dtrace, see:
* The book "BPF Performance Tools" by Brendan Gregg, which discusses bpftrace at length.
* <https://ebpf.io/get-started/>
* [Illumos' dtrace book](https://illumos.org/books/dtrace/preface.html)
