# Test whether we can run a NixOS container inside a Nix build using systemd-nspawn.
{ lib, nixpkgs, ... }:

{
  name = "containers";

  nodes =
    {
      host =
        { config, lib, pkgs, nodes, ... }:
        { virtualisation.writableStore = true;
          virtualisation.diskSize = 2048;
          virtualisation.additionalPaths =
            [ pkgs.stdenvNoCC
              (import ./systemd-nspawn.nix { inherit nixpkgs; }).toplevel
            ];
          virtualisation.memorySize = 4096;
          nix.settings = {
            substituters = lib.mkForce [ ];
            use-cgroups = true;
          };
          nix.extraOptions =
            ''
              extra-experimental-features = nix-command auto-allocate-uids cgroups
              extra-system-features = uid-range
            '';
          nix.nixPath = [ "nixpkgs=${nixpkgs}" ];
        };
    };

  testScript = { nodes }: ''
    start_all()

    host.succeed("nix --version >&2")

    # Test that 'id' gives the expected result in various configurations.

    # Existing UIDs, sandbox.
    host.succeed("nix build -v --no-auto-allocate-uids --no-use-cgroups --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-1")
    host.succeed("[[ $(cat ./result) = 'uid=1000(nixbld) gid=100(nixbld) groups=100(nixbld)' ]]")

    # Existing UIDs, no sandbox.
    host.succeed("nix build -v --no-auto-allocate-uids --no-use-cgroups --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-2")
    host.succeed("[[ $(cat ./result) = 'uid=30001(nixbld1) gid=30000(nixbld) groups=30000(nixbld)' ]]")

    # Auto-allocated UIDs, sandbox but no cgroups.
    host.fail("nix build -v --auto-allocate-uids --no-use-cgroups --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-3")

    # Auto-allocated UIDs, no sandbox but no cgroups.
    host.fail("nix build -v --auto-allocate-uids --no-use-cgroups --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-4")

    # Auto-allocated UIDs, UID range, sandbox, via daemon.
    host.succeed("nix build -v --auto-allocate-uids --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-5 --arg uidRange true")
    host.succeed("[[ $(cat ./result) = 'uid=0(root) gid=0(root) groups=0(root)' ]]")

    # Auto-allocated UIDs, UID range, no sandbox, with and without daemon.
    host.fail("nix build -v --auto-allocate-uids --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-6 --arg uidRange true")
    host.fail("nix build -v --auto-allocate-uids --no-use-cgroups --no-sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-7 --arg uidRange true")

    # Run systemd-nspawn in a Nix build, via daemon.
    host.succeed("nix build -vv --auto-allocate-uids --sandbox -L --offline --impure --file ${./systemd-nspawn.nix} --argstr nixpkgs ${nixpkgs}")
    host.succeed("[[ $(cat ./result/msg) = 'Hello World' ]]")

    # Auto-allocated UIDs, UID range, sandbox, WITHOUT daemon (so-called: Nix as root), IN `systemd-run` transient scope.
    # `systemd-run` is CRITICAL to run this successfully.
    host.succeed("systemd-run --same-dir --wait -E NIX_REMOTE=local -p Delegate=yes -p DelegateSubgroup=supervisor nix build -v --auto-allocate-uids --sandbox -L --offline --impure --file ${./id-test.nix} --argstr name id-test-8 --arg uidRange true -I nixpkgs=${nixpkgs}")
    host.succeed("[[ $(cat ./result) = 'uid=0(root) gid=0(root) groups=0(root)' ]]")

    # Run systemd-nspawn in a Nix build, WITHOUT daemon (so-called: Nix as root) IN `systemd-run`.
    # `systemd-run` is CRITICAL to run this successfully.
    host.succeed("systemd-run --same-dir --wait -E NIX_REMOTE=local -p Delegate=yes -p DelegateSubgroup=supervisor nix build -v --auto-allocate-uids --sandbox -L --offline --impure --file ${./systemd-nspawn.nix} --argstr nixpkgs ${nixpkgs} -I nixpkgs=${nixpkgs}")
    host.succeed("[[ $(cat ./result/msg) = 'Hello World' ]]")
  '';

}
