---
name: hashed-mirrors
internalName: hashedMirrors
type: Strings
default: []
---
A list of web servers used by `builtins.fetchurl` to obtain files by
hash. Given a hash type *ht* and a base-16 hash *h*, Lix will try to
download the file from *hashed-mirror*/*ht*/*h*. This allows files to
be downloaded even if they have disappeared from their original URI.
For example, given an example mirror `http://tarballs.nixos.org/`,
when building the derivation

```nix
builtins.fetchurl {
  url = "https://example.org/foo-1.2.3.tar.xz";
  sha256 = "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae";
}
```

Lix will attempt to download this file from
`http://tarballs.nixos.org/sha256/2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae`
first. If it is not available there, if will try the original URI.
