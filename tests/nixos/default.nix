{ self, lib, nixpkgs, nixpkgsFor }:

let

  nixos-lib = import (nixpkgs + "/nixos/lib") { };

  # https://nixos.org/manual/nixos/unstable/index.html#sec-calling-nixos-tests
  runNixOSTestFor = system: test:
    (nixos-lib.runTest {
      imports = [ test ];
      hostPkgs = nixpkgsFor.${system}.native;
      defaults = {
        nixpkgs.pkgs = nixpkgsFor.${system}.native;
        nix.checkAllErrors = false;
        # nixos-option fails to build with lix and no tests use any of the tools
        system.disableInstallerTools = true;
      };
      _module.args.nixpkgs = nixpkgs;
      _module.args.system = system;
      _module.args.self = self;
    })
    // {
      # allow running tests against older nix versions via `nix eval --apply`
      # Example:
      #   nix build "$(nix eval --raw --impure .#hydraJobs.tests.fetch-git --apply 't: (t.forNix "2.19.2").drvPath')^*"
      forNix = nixVersion: runNixOSTestFor system {
        imports = [test];
        defaults.nixpkgs.overlays = [(curr: prev: {
          nix = (builtins.getFlake "nix/${nixVersion}").packages.${system}.nix;
        })];
      };
    };

  # Checks that a NixOS configuration does not contain any references to our
  # locally defined Nix version.
  checkOverrideNixVersion = { pkgs, lib, ... }: {
    # pkgs.nix: The new Nix in this repo
    # We disallow it, to make sure we don't accidentally use it.
    system.forbiddenDependenciesRegexes = [ (lib.strings.escapeRegex "nix-${pkgs.nix.version}") ];
  };
in

{
  local-releng = runNixOSTestFor "x86_64-linux" ../../releng/local;

  authorization = runNixOSTestFor "x86_64-linux" ./authorization.nix;

  remoteBuilds = runNixOSTestFor "x86_64-linux" ./remote-builds.nix;

  s3-binary-cache = runNixOSTestFor "x86_64-linux" ./s3-cache.nix;

  # Test our Nix as a client against remotes that are older

  remoteBuilds_remote_2_3 = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuilds_remote_2_3";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  };

  remoteBuilds_remote_2_18 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_remote_2_18";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_18;
    };
  });

  # Let's ensure that reasonably popular shells are tested for remote building.

  remoteBuildsNushell = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_nushell";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      users.users.root.shell = pkgs.nushell;
    };
  });

  remoteBuildsBusybox = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_busybox";
    imports = [ ./remote-builds.nix ];
    builders.config = { lib, pkgs, ... }: {
      users.users.root.shell = pkgs.busybox;
    };
  });

  # Test our Nix as a builder for clients that are older

  remoteBuilds_local_2_3 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_local_2_3";
    imports = [ ./remote-builds.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  });

  remoteBuilds_local_2_18 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuilds_local_2_18";
    imports = [ ./remote-builds.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_18;
    };
  });

  # End remoteBuilds tests

  remoteBuildsSshNg = runNixOSTestFor "x86_64-linux" ./remote-builds-ssh-ng.nix;

  # Test building with a non‐`root` user on the remote

  remoteBuildsSshNgNonRoot = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuildsSshNgNonRoot";
    imports = [ ./remote-builds-ssh-ng.nix ];
    builders.config = {
      users.users.test-user = {
        isNormalUser = true;
      };
    };
    sshUser = "test-user";
  };

  # Test our Nix as a client against remotes that are older

  remoteBuildsSshNg_remote_2_18 = runNixOSTestFor "x86_64-linux" {
    name = "remoteBuildsSshNg_remote_2_18";
    imports = [ ./remote-builds-ssh-ng.nix ];
    builders.config = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_18;
    };
  };

  # Test our Nix as a builder for clients that are older

  # FIXME: these tests don't work yet
  /*
  remoteBuildsSshNg_local_2_3 = runNixOSTestFor "x86_64-linux" ({ lib, pkgs, ... }: {
    name = "remoteBuildsSshNg_local_2_3";
    imports = [ ./remote-builds-ssh-ng.nix ];
    nodes.client = { lib, pkgs, ... }: {
      imports = [ checkOverrideNixVersion ];
      nix.package = lib.mkForce pkgs.nixVersions.nix_2_3;
    };
  });

  # TODO: (nixpkgs update) remoteBuildsSshNg_local_2_18 = ...
  */

  nix-copy-closure = runNixOSTestFor "x86_64-linux" ./nix-copy-closure.nix;

  nix-copy = runNixOSTestFor "x86_64-linux" ./nix-copy.nix;

  nix-upgrade-nix = runNixOSTestFor "x86_64-linux" ./nix-upgrade-nix.nix;

  nssPreload = runNixOSTestFor "x86_64-linux" ./nss-preload.nix;

  githubFlakes = runNixOSTestFor "x86_64-linux" ./github-flakes.nix;

  sourcehutFlakes = runNixOSTestFor "x86_64-linux" ./sourcehut-flakes.nix;

  tarballFlakes = runNixOSTestFor "x86_64-linux" ./tarball-flakes.nix;

  containers = runNixOSTestFor "x86_64-linux" ./containers/containers.nix;

  cgroups = runNixOSTestFor "x86_64-linux" ./cgroups;

  setuid = lib.genAttrs
    ["i686-linux" "x86_64-linux"]
    (system: runNixOSTestFor system ./setuid/setuid.nix);

  fetch-git = runNixOSTestFor "x86_64-linux" ./fetch-git;

  symlinkResolvconf = runNixOSTestFor "x86_64-linux" ./symlink-resolvconf.nix;

  noNewPrivilegesInSandbox = runNixOSTestFor "x86_64-linux" ./no-new-privileges/sandbox.nix;

  noNewPrivilegesOutsideSandbox = runNixOSTestFor "x86_64-linux" ./no-new-privileges/no-sandbox.nix;

  broken-userns = runNixOSTestFor "x86_64-linux" ./broken-userns.nix;

  coredumps = runNixOSTestFor "x86_64-linux" ./coredumps;

  io_uring = runNixOSTestFor "x86_64-linux" ./io_uring;

  fetchurl = runNixOSTestFor "x86_64-linux" ./fetchurl.nix;

  chown-to-user = runNixOSTestFor "x86_64-linux" ./chown-to-user.nix;
}
