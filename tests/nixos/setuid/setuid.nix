# Verify that Linux builds cannot create setuid or setgid binaries.

{ lib, config, nixpkgs, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  fchmodat2-builder = pkgs.runCommandCC "fchmodat2-suid" {
    passAsFile = [ "code" ];
    code = builtins.readFile ./fchmodat2-suid.c;
    # Doesn't work with -O0, shuts up the warning about that.
    hardeningDisable = [ "fortify" ];
  } ''
    mkdir -p $out/bin/
    $CC -x c "$codePath" -O0 -g -o $out/bin/fchmodat2-suid
  '';

in
{
  name = "setuid";

  nodes.machine =
    { config, lib, pkgs, ... }:
    { virtualisation.writableStore = true;
      nix.settings.substituters = lib.mkForce [ ];
      nix.nixPath = [ "nixpkgs=${lib.cleanSource pkgs.path}" ];
      virtualisation.additionalPaths = [
        pkgs.stdenvNoCC
        pkgs.pkgsi686Linux.stdenvNoCC
        fchmodat2-builder
      ];
      # need at least 6.6 to test for fchmodat2
      boot.kernelPackages = pkgs.linuxKernel.packages.linux_6_6;

    };

  testScript = { nodes }: ''
    # fmt: off
    start_all()

    with subtest("fchmodat2 suid regression test"):
      machine.succeed("""
      nix-build -E '(with import <nixpkgs> {}; runCommand "fchmodat2-suid" {
        BUILDER = builtins.storePath ${fchmodat2-builder};
      } "
        exec \\"$BUILDER\\"/bin/fchmodat2-suid
      ")'
      """)

    # Copying to /tmp should succeed.
    machine.succeed(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" {} "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")

    # Creating a setuid binary should fail.
    machine.fail(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" {} "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      chmod 4755 /tmp/id
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")

    # Creating a setgid binary should fail.
    machine.fail(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" {} "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      chmod 2755 /tmp/id
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")

    # The checks should also work on 32-bit binaries.
    machine.fail(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> { system = "i686-linux"; }; runCommand "foo" {} "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      chmod 2755 /tmp/id
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")

    # The tests above use fchmodat(). Test chmod() as well.
    machine.succeed(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      perl -e \"chmod 0666, qw(/tmp/id) or die\"
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 666 ]]')

    machine.succeed("rm /tmp/id")

    machine.fail(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      perl -e \"chmod 04755, qw(/tmp/id) or die\"
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")

    # And test fchmod().
    machine.succeed(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      perl -e \"my \\\$x; open \\\$x, qw(/tmp/id); chmod 01750, \\\$x or die\"
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 1750 ]]')

    machine.succeed("rm /tmp/id")

    machine.fail(r"""
    nix-build --no-sandbox -E '(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
      mkdir -p $out
      cp ${pkgs.coreutils}/bin/id /tmp/id
      perl -e \"my \\\$x; open \\\$x, qw(/tmp/id); chmod 04777, \\\$x or die\"
    ")'
    """.strip())

    machine.succeed('[[ $(stat -c %a /tmp/id) = 555 ]]')

    machine.succeed("rm /tmp/id")
  '';
}
