---
synopsis: "Global certificate authorities are copied inside the builder's environment"
issues: [gh#12698, fj#885]
cls: [3765]
category: Fixes
credits: [raito, emilazy]
---

Previously, CA certificates were only installed at
`/etc/ssl/certs/ca-certificates.crt` for sandboxed builds on Linux.

This setup was insufficient in light of recent changes in `nixpkgs`, which now
enforce HTTPS usage for `fetchurl`, even for fixed-output derivations, to
mitigate confidentiality risks such as `netrc` or credentials leakage.
`nixpkgs` still make use of a special package called `cacerts` which contains a
copy of the CA certificates maintained by Nixpkgs and added as a reference for
TLS-enabled fetchers.

As a result, having a consistent and trusted certificate authority in all
builder environments is becoming more essential.

On `nix-darwin`, the `NIX_SSL_CERT_FILE` environment variable is always
explicitly defined, but it is ignored by the sandbox setup.

Simultaneously, Nix evaluates and propagates impure environment variables via
`lib.proxyImpureEnvVars`, meaning that if `NIX_SSL_CERT_FILE` is set (which
influences the default value for `ssl-cert-file`), it will be forwarded
unchanged into the builder environment.

However, on Linux, Nix also *copies* the CA file into the sandbox, creating a
discrepancy between the value of `NIX_SSL_CERT_FILE` and the actual trusted
certificate path used during the build.

This divergence caused confusion and was partially addressed by attempts to
whitelist the CA path in the Darwin sandbox (see cl/2906), but that approach
involved a non-trivial path canonicalization step and is not as general as this one.

To address this properly, we now emit a warning and override
`NIX_SSL_CERT_FILE` inside the builder, explicitly pointing it to the CA file
copied into the sandbox.

This eliminates ambiguity between `NIX_SSL_CERT_FILE`
and `ssl-cert-file`, ensuring consistent trust anchors across platforms.

This warning might become a hard error as we figure out what to do regarding
`lib.proxyImpureEnvVars` in nixpkgs.

The behavior has been verified across sandboxed and unsandboxed builds on both
Linux and Darwin.

As a consequence of this change, approximately 500 KB of CA certificate data is
now unconditionally copied into the build directory for fixed-output
derivations.

While this ensures consistent trust verification without having to restart the
daemon after system upgrades, it may introduce a slight overhead in build
performance. At present, no optimizations have been implemented to avoid this
copy, but if this overhead proves noticeable in your workflows, please open an
issue so we can evaluate and possibly implement different strategies to render
trust anchors visible.
