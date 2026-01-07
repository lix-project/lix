---
synopsis: "Make `log-format` a setting"
cls: [4686]
category: "Features"
credits: [Qyriad]
---

The [`--log-format` CLI option](@docroot@/command-ref/opt-common.md#opt-log-format) can now be set in [`nix.conf`](@docroot@/command-ref/conf-file.md#conf-log-format)!
For example, you can now persistently enable the `multiline-with-logs` log format [added in Lix 2.91](@docroot@/release-notes/rl-2.91.md) by adding the following to your `nix.conf`:

```conf
log-format = multiline-with-logs
```

Or the equivalent in a NixOS configuration:
```nix
{
  nix.settings.log-format = "multiline-with-logs";
}
```
