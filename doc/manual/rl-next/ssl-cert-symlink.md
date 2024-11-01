---
synopsis: Ignore broken `/etc/ssl/certs/ca-certificates.crt` symlink
issues: [fj#560]
cls: [2144]
category: Fixes
credits: lilyball
---

[`ssl-cert-file`](@docroot@/command-ref/conf-file.md#conf-ssl-cert-file) now checks its default
value for a broken symlink before using it. This fixes a problem on macOS where uninstalling
nix-darwin may leave behind a broken symlink at `/etc/ssl/certs/ca-certificates.crt` that was
stopping Lix from using the cert at `/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt`.
