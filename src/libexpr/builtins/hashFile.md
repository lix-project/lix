---
name: hashFile
args: [type, p]
---
Return a base-16 representation of the cryptographic hash of the
file at path *p*. The hash algorithm specified by *type* must be one
of `"md5"`, `"sha1"`, `"sha256"` or `"sha512"`.
