---
synopsis: Stack traces now summarize involved derivations at the bottom
cls: [4493]
category: Improvements
credits: [Qyriad]
---

When evaluation errors and a stack trace is printed,

For example, if I add Nheko to a NixOS `environment.systemPackages` without adding `olm-3.2.16` `nixpkgs.config.permittedInsecurePackages`, then without `--show-trace`, I previously got this:

```
error:
       … while calling the 'head' builtin
         at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/attrsets.nix:1701:13:
         1700|           if length values == 1 || pred here (elemAt values 1) (head values) then
         1701|             head values
             |             ^
         1702|           else

       … while evaluating the attribute 'value'
         at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/modules.nix:1118:7:
         1117|     // {
         1118|       value = addErrorContext "while evaluating the option `${showOption loc}':" value;
             |       ^
         1119|       inherit (res.defsFinal') highestPrio;

       (stack trace truncated; use '--show-trace' to show the full trace)

       error: Package ‘olm-3.2.16’ in /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/pkgs/by-name/ol/olm/package.nix:37 is marked as insecure, refusing to evaluate.

       < -snip the whole explanation about olm's CVEs- >
```

This doesn't tell me anything about where `olm-3.2.16` came from.
With `--show-trace`, there's 1 155 lines to sift through, but does contain lines like "while evaluating derivation 'nheko-0.12.1'".

With this change, those lines are summarized and collected at the bottom, regardless of `--show-trace`:

```
error:
       … while calling the 'head' builtin
         at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/attrsets.nix:1701:13:
         1700|           if length values == 1 || pred here (elemAt values 1) (head values) then
         1701|             head values
             |             ^
         1702|           else

       … while evaluating the attribute 'value'
         at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/modules.nix:1118:7:
         1117|     // {
         1118|       value = addErrorContext "while evaluating the option `${showOption loc}':" value;
             |       ^
         1119|       inherit (res.defsFinal') highestPrio;

       (stack trace truncated; use '--show-trace' to show the full trace)

       error: Package ‘olm-3.2.16’ in /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/pkgs/by-name/ol/olm/package.nix:37 is marked as insecure, refusing to evaluate.


       < -snip the whole explanation about olm's CVEs- >


       note: trace involved the following derivations:
       derivation 'etc'
       derivation 'dbus-1'
       derivation 'system-path'
       derivation 'nheko-0.12.1'
       derivation 'mtxclient-0.10.1'
```

Now we finally know that olm was evaluated because of Nheko, without sifting through *thousands* of lines of error message.
