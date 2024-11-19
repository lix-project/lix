---
name: tarball-ttl
internalName: tarballTtl
type: unsigned int
default: 3600 # 60 * 60
---
The number of seconds a downloaded tarball is considered fresh. If
the cached tarball is stale, Lix will check whether it is still up
to date using the ETag header. Lix will download a new version if
the ETag header is unsupported, or the cached ETag doesn't match.

Setting the TTL to `0` forces Lix to always check if the tarball is
up to date.

Lix caches tarballs in `$XDG_CACHE_HOME/nix/tarballs`.

Files fetched via `NIX_PATH`, `fetchGit`, `fetchMercurial`,
`fetchTarball`, and `fetchurl` respect this TTL.
