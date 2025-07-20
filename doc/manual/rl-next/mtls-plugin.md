---
synopsis: "mTLS store connections via a plugin"
issues: []
cls: [3754, 3696, 3697, 3698]
category: Improvements
credits: [raito, horrors, mic92, vlaci, nkk0]
---

To support use cases requiring mutual TLS (mTLS) authentication when connecting
to remote Nix stores, e.g. private stores, we have introduced a **contributed**
mTLS plugin extending the Lix store interface.

This design follows an extensibility model which was brought up [by a proposal
of making Kerberos authentication possible in Lix
directly](https://gerrit.lix.systems/c/lix/+/3637).

This mTLS plugin serves as a concrete example of how store connection
mechanisms can be modularized through external plugins, without extending Lix
core. This idea can be generalized to integrate automatic certificate renewal
or advanced integrations with secrets engine or posture checks.

It enables custom TLS client certificates to be used for authenticating against
a remote store that enforces mTLS.

To use the plugin, configure Lix manually by setting in your `nix.conf`:

```
plugin-files = /a/path/to/libplugin_mtls_store.so
```

Currently, this must be done explicitly. In the future, Nixpkgs will provide a
mechanism to reference an up-to-date and curated set of plugins automatically.

Making plugins easily consumable outside of Nixpkgs (e.g., from external plugin
registries or binary distributions) remains an open question and will require
further design.

Contributed plugins come with significantly reduced **stability** and
**maintenance** guarantees compared to the Lix core. We encourage users who
depend on a given plugin to take on maintenance responsibilities and apply for
ownership within the Lix mono-repository. These plugins are subject to removal
at any time.
