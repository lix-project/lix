---
synopsis: "Allow `nix store diff-closures` to output JSON"
issues: []
cls: [2360]
category: Improvements
credits: [pamplemousse]
---

Add the `--json` option to the `nix store diff-closures` command to allow users to collect diff information into a machine readable format.

```bash
$ build/lix/nix/nix store diff-closures --json /run/current-system /nix/store/n1prick95pihd4lkv58nn3pzg1yivcdb-neovim-0.10.4/bin/nvim | jq | head -n 23
{
  "packages": {
    "02overridedns": {
      "sizeDelta": -688,
      "versionsAfter": [],
      "versionsBefore": [
        ""
      ]
    },
    "50-coredump.conf": {
      "sizeDelta": -1976,
      "versionsAfter": [],
      "versionsBefore": [
        ""
      ]
    },
    "Diff": {
      "sizeDelta": -514864,
      "versionsAfter": [],
      "versionsBefore": [
        "0.4.1"
      ]
    },
```
