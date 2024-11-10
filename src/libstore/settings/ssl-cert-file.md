---
name: ssl-cert-file
internalName: caFile
type: Path
defaultExpr: 'getDefaultSSLCertFile()'
defaultText: '*machine-specific*'
---
The path of a file containing CA certificates used to
authenticate `https://` downloads. Lix by default will use
the first of the following files that exists:

1. `/etc/ssl/certs/ca-certificates.crt`
2. `/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt`

The path can be overridden by the following environment
variables, in order of precedence:

1. `NIX_SSL_CERT_FILE`
2. `SSL_CERT_FILE`
