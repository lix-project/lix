---
synopsis: "Collect Flakes untrusted settings into one prompt"
cls: [2921]
issues: [fj#682]
category: "Improvements"
credits: [isabelroses, raito, horrors]
---

When working with Flakes containing untrusted settings, a prompt is shown for each setting, asking whether to vet or approve it. This looks like:

```
❯ nix flake lock
warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
The following settings require your decision:
- allow-dirty = false
- sandbox = false
Do you want to allow configuration settings to be applied?
This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all)
```

In Flakes with a large number of settings to approve or reject, this process can become tedious as each option must be handled individually.

To address this, all untrusted settings are now consolidated into a single prompt: allowing for bulk acceptance permanently or not, rejection, or detailed review. For example:

### Scrutiny scenario

```console
❯ nix flake lock
warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
The following settings require your decision:
- allow-dirty = false
- sandbox = false
Do you want to allow configuration settings to be applied?
This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) n
warning: you can set 'accept-flake-config' to 'false' to automatically reject configuration options supplied by flakes
Do you want to allow setting 'allow-dirty = false'? (yes for now/Allow always/no for now) y
Do you want to allow setting 'sandbox = false'? (yes for now/Allow always/no for now) n
```

### Reject everything scenario

```console
❯ nix flake lock
warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
The following settings require your decision:
- allow-dirty = false
- sandbox = false
Do you want to allow configuration settings to be applied?
This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) N
Rejecting all untrusted nix.conf entries
warning: you can set 'accept-flake-config' to 'false' to automatically reject configuration options supplied by flakes
```

### Accept everything scenario

```console
❯ nix flake lock
warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
The following settings require your decision:
- allow-dirty = false
- sandbox = false
Do you want to allow configuration settings to be applied?
This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) y
```

### Accept everything PERMANENTLY scenario

Note that accepting everything permanently will authorize these options for any
further operations.

The file containing this trust information is usually located in
`~/.local/share/nix/trusted-settings.json` and can be edited manually to revoke
this permission until Lix provides a first-class command for this manipulation.

```console
❯ nix flake lock
warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
The following settings require your decision:
- allow-dirty = false
- sandbox = false
Do you want to allow configuration settings to be applied?
This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) A
```
