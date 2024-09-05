---
synopsis: Removing the `.` default argument passed to the `nix fmt` formatter
issues: []
prs: [11438]
cls: [1902]
category: Breaking Changes
credits: zimbatm
---

The underlying formatter no longer receives the ". " default argument when `nix fmt` is called with no arguments.

This change was necessary as the formatter wasn't able to distinguish between
a user wanting to format the current folder with `nix fmt .` or the generic
`nix fmt`.

The default behaviour is now the responsibility of the formatter itself, and
allows tools such as treefmt to format the whole tree instead of only the
current directory and below.

This may cause issues with some formatters: nixfmt, nixpkgs-fmt and alejandra currently format stdin when no arguments are passed.

Here is a small wrapper example that will restore the previous behaviour for such a formatter:

```nix
{
  outputs = { self, nixpkgs, systems }:
    let
      eachSystem = nixpkgs.lib.genAttrs (import systems) (system: nixpkgs.legacyPackages.${system});
    in
    {
      formatter = eachSystem (pkgs:
        pkgs.writeShellScriptBin "formatter" ''
          if [[ $# = 0 ]]; set -- .; fi
          exec "${pkgs.nixfmt-rfc-style}/bin/nixfmt "$@"
        '');
    };
}
```
