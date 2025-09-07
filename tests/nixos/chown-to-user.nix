{ lib, config, pkgs, ... }:

let
  failedNormal = config: pkgs.writeText "failed.nix" ''
    let utils = builtins.storePath ${config.system.build.extraUtils}; in
    derivation {
      name = "failed";
      system = builtins.currentSystem;
      PATH = "''${utils}/bin";
      builder = "''${utils}/bin/sh";
      args = [ "-c" "mkdir dir; echo test > dir/file" ];
    }
  '';

  failedBuiltin = pkgs.writeText "failed.nix" ''
    derivation {
      name = "failed";
      system = builtins.currentSystem;
      builder = "builtin:fetchurl";
      url = "http://localhost/foo";
      outputHashMode = "flat";
    }
  '';
in
{
  name = "chown-to-user";

  nodes = {
    machine = { config, lib, pkgs, ... }: {
      virtualisation.writableStore = true;
      virtualisation.additionalPaths = [ config.system.build.extraUtils ];

      users.users.test = {
        isNormalUser = true;
        group = "test";
      };
      users.groups.test = {};
      nix.nrBuildUsers = 1;
    };
  };

  testScript = { nodes, ... }: ''
    import re

    machine.wait_for_unit("multi-user.target")

    # builds using the daemon chown tempdirs
    out = machine.fail("runuser -u test -- nix-build ${failedNormal nodes.machine} --keep-failed 2>&1")
    dir = re.search("keeping build directory '(.+?)'", out)
    assert dir
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}").strip() == "root:nixbld:755"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b").strip() == "test:test:700"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b/dir").strip() == "test:test:755"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b/dir/file").strip() == "test:test:644"

    # builds not using the daemon do not chown tempdirs
    out = machine.fail("NIX_REMOTE=local nix-build ${failedNormal nodes.machine} --keep-failed 2>&1")
    dir = re.search("keeping build directory '(.+?)'", out)
    assert dir
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}").strip() == "root:nixbld:755"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b").strip() == "nixbld1:nixbld:700"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b/dir").strip() == "nixbld1:nixbld:755"
    assert machine.succeed(f"stat -c %U:%G:%a {dir[1]}/b/dir/file").strip() == "nixbld1:nixbld:644"

    # builds using builtin builders using the daemon do not keep tempdirs
    out = machine.fail("runuser -u test -- nix-build ${failedBuiltin} --keep-failed 2>&1")
    dir = re.search("keeping build directory '(.+?)'", out)
    assert not dir

    # builds using builtin builders not using the daemon do not keep tempdirs
    out = machine.fail("NIX_REMOTE=local nix-build ${failedBuiltin} --keep-failed 2>&1")
    dir = re.search("keeping build directory '(.+?)'", out)
    assert not dir
  '';
}
