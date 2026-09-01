---
synopsis: "Mant nix3 command no longer build their arguments"
cls: []
category: Breaking Changes
credits: [horrors]
---

These nix3 cli commands no longer build the installables given to them directly:
- `nix store` subcommands `dump-path`, `make-content-addressed`, `copy-sigs`, `sign`, `delete`, `verify`
- `nix copy`
- `nix path-info`

Instead they expect paths given on the command line to already exist, independently of how they were provided (whether as store paths, flake attribute paths, outputs of files, you name it).
We've chosen to change this because the previous behavior could be quite confusing, for example when deleting a path that was not present in the local store (which would be downloaded or even built only to immediately delete it again).
If you relied on this implicit building behavior you will need to **update your workflows** to explicitly build all necessary path before passing them on.
