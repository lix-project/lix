# [Pasta](https://passt.top/passt/about/): a network sandbox for fixed-output derivations

## Introduction

This section only applies to **Linux systems** as Pasta is a Linux-only measure.

Since [CVE-2025-46416](https://lix.systems/blog/2025-06-24-lix-cves/), the Lix project decided to adopt [Pasta](https://passt.top/passt/about/) for all fixed-output derivations, protecting against various attack vectors such as UNIX abstract domain sockets or more manipulation at the network layer from a malicious fixed-output derivation code.

Pasta acts as a translation layer between a layer-2 network interface and layer-4 sockets (TCP, UDP, ICMP/ICMPv6 echo) on the host. It requires no special privileges and can serve as a alternative to [SLiRP](https://en.wikipedia.org/wiki/Slirp) which was used [by Guix to mitigate the same problem](https://codeberg.org/guix/guix/commit/fb42611b8f27960304db5a1c0d33b8371dcde2a8).

## How to disable Pasta?

It's sufficient to pass `pasta-path = ""` in your `/etc/nix/nix.conf` or on the command line `--pasta-path ""` of a Lix invocation.

## Known issues surrounding Pasta

- Only the first DNS server in `/etc/resolv.conf` is considered: failover is not possible.
- [Reduced feature set compared to the Linux kernel](https://passt.top/passt/about/#features)
- [Performance overhead in multi-gigabits contexts and IMIX MTUs](https://passt.top/passt/about/#performance_1)
