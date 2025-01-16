---
synopsis: "`nix-env --install` now accepts a `--priority` flag"
cls: [2607]
category: Features
credits: andrewhamon
---

`nix-env --install` now has an optional `--priority` flag.

Previously, it was only possible to specify a priority by adding a
`meta.priority` attribute to a derivation. `meta` attributes only exist during
eval, so that wouldn't work for installing a store path. It was also possible
to change a priority after initial installation using `nix-env --set-flag`,
however if there is already a conflict that needs to be resolved via priorities,
this will not work.

Now, a priority can be set at install time using `--priority`, which allows for
cleanly overriding the priority at install time.

#### Example

```console
$ nix-build
$ nix-env --install --priority 100 ./result
```
