test@{ config, lib, hostPkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Trivial Nix expression to build remotely.
  expr = config: nr: pkgs.writeText "expr.nix"
    ''
      let utils = builtins.storePath ${config.system.build.extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        system = "i686-linux";
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "${
          lib.concatStringsSep "; " [
            ''if [[ -n $NIX_LOG_FD ]]''
            ''then echo '@nix {\"action\":\"setPhase\",\"phase\":\"buildPhase\"}' >&''$NIX_LOG_FD''
            "fi"
            "echo Hello"
            "mkdir $out"
            "cat /proc/sys/kernel/hostname > $out/host"
          ]
        }" ];
        outputs = [ "out" ];
      }
    '';
in

{
  options = {
    builders.config = lib.mkOption {
      type = lib.types.deferredModule;
      description = ''
        Configuration to add to the builder nodes.
      '';
      default = { };
    };

    sshUser = lib.mkOption {
      type = lib.types.str;
      description = ''
        Builder user to run remote builds as.
      '';
      default = "root";
    };
  };

  config = {
    name = lib.mkDefault "remote-builds-ssh-ng";

    nodes =
      { builder =
        { config, pkgs, ... }:
        {
          imports = [ test.config.builders.config ];
          services.openssh.enable = true;
          virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ config.system.build.extraUtils ];
          nix.settings.sandbox = true;
          nix.settings.substituters = lib.mkForce [ ];
        };

        client =
          { config, lib, pkgs, ... }:
          {
            nix.settings.max-jobs = 0; # force remote building
            nix.distributedBuilds = true;
            nix.buildMachines =
              [ { hostName = "builder";
                  inherit (test.config) sshUser;
                  sshKey = "/root/.ssh/id_ed25519";
                  system = "i686-linux";
                  maxJobs = 1;
                  protocol = "ssh-ng";
                }
              ];
            virtualisation.writableStore = true;
            virtualisation.additionalPaths = [ config.system.build.extraUtils ];
            nix.settings.substituters = lib.mkForce [ ];
            programs.ssh.extraConfig = "ConnectTimeout 30";
          };
      };

    testScript = { nodes }: ''
      # fmt: off
      import subprocess

      start_all()

      builder.succeed("systemctl start network-online.target")
      client.succeed("systemctl start network-online.target")
      builder.wait_for_unit("network-online.target")
      client.wait_for_unit("network-online.target")

      # Create an SSH key on the client.
      subprocess.run([
        "${hostPkgs.openssh}/bin/ssh-keygen", "-t", "ed25519", "-f", "key", "-N", ""
      ], capture_output=True, check=True)
      client.succeed("mkdir -p -m 700 /root/.ssh")
      client.copy_from_host("key", "/root/.ssh/id_ed25519")
      client.succeed("chmod 600 /root/.ssh/id_ed25519")

      # Install the SSH key on the builder.
      ssh_user = "${test.config.sshUser}"
      ssh_directory = "${nodes.builder.users.users.${test.config.sshUser}.home}/.ssh"
      builder.succeed(f"mkdir -p -m 700 {ssh_directory}")
      builder.succeed(f"chown {ssh_user}:root {ssh_directory}")
      builder.copy_from_host("key.pub", f"{ssh_directory}/authorized_keys")
      builder.wait_for_unit("sshd.service")

      out = client.fail("nix-build ${expr nodes.client 1} 2>&1")
      assert "Host key verification failed." in out, f"No host verification error:\n{out}"
      assert f"'ssh-ng://{ssh_user}@builder'" in out, f"No details about which host:\n{out}"

      client.succeed(f"ssh -o StrictHostKeyChecking=no {ssh_user}@{builder.name} 'echo hello world' >&2")

      # Perform a build
      out = client.succeed("nix-build ${expr nodes.client 1} 2> build-output")

      # Verify that the build was done on the builder
      builder.succeed(f"test -e {out.strip()}")

      # Print the build log, prefix the log lines to avoid nix intercepting lines starting with @nix
      buildOutput = client.succeed("sed -e 's/^/build-output:/' build-output")
      print(buildOutput)

      # Make sure that we get the expected build output
      client.succeed("grep -qF Hello build-output")

      # We don't want phase reporting in the build output
      client.fail("grep -qF '@nix' build-output")

      # Get the log file
      client.succeed(f"nix-store --read-log {out.strip()} > log-output")
      # Prefix the log lines to avoid nix intercepting lines starting with @nix
      logOutput = client.succeed("sed -e 's/^/log-file:/' log-output")
      print(logOutput)

      # Check that we get phase reporting in the log file
      client.succeed("grep -q '@nix {\"action\":\"setPhase\",\"phase\":\"buildPhase\"}' log-output")
    '';
  };
}
