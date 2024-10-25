# Test that ‘nix copy’ works over ssh.
# Run interactively with:
# rm key key.pub; nix run .#hydraJobs.tests.nix-copy.driverInteractive

{ lib, config, nixpkgs, hostPkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  pkgA = pkgs.cowsay;
  pkgB = pkgs.wget;
  pkgC = pkgs.hello;
  pkgD = pkgs.tmux;

in {
  name = "nix-copy";

  enableOCR = true;

  nodes =
    { client =
        { config, lib, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ pkgA pkgD.drvPath ];
          nix.settings.substituters = lib.mkForce [ ];
          nix.settings.experimental-features = [ "nix-command" ];
          services.getty.autologinUser = "root";
          programs.ssh.extraConfig = ''
            Host *
                ControlMaster auto
                ControlPath ~/.ssh/master-%h:%r@%n:%p
                ControlPersist 15m
          '';
        };

      server =
        { config, pkgs, ... }:
        { services.openssh.enable = true;
          services.openssh.settings.PermitRootLogin = "yes";
          users.users.root.hashedPasswordFile = lib.mkForce null;
          virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ pkgB pkgC ];
        };
    };

  testScript = { nodes }: ''
    # fmt: off
    import subprocess

    # Create an SSH key on the client.
    subprocess.run([
      "${pkgs.openssh}/bin/ssh-keygen", "-t", "ed25519", "-f", "key", "-N", ""
    ], capture_output=True, check=True)

    start_all()

    server.succeed("systemctl start network-online.target")
    client.succeed("systemctl start network-online.target")
    server.wait_for_unit("network-online.target")
    client.wait_for_unit("network-online.target")
    server.wait_for_unit("multi-user.target")
    client.wait_for_unit("multi-user.target")

    server.wait_for_unit("sshd.service")

    # Copy the closure of package A from the client to the server using password authentication,
    # and check that all prompts are visible
    # NOTE: this used to also check password prompts, but the test implementation was monumentally
    # fragile (and hence broke constantly). since ssh interacts with /dev/tty directly fixing this
    # requires some proper cli automation, which we do not have. however, since ssh interacts with
    # /dev/tty directly and the host key message goes there too, we don't even need to check this.
    server.fail("nix-store --check-validity ${pkgA}")
    client.succeed("echo | script -f /dev/stdout -c 'nix copy --to ssh://server ${pkgA}' | grep 'continue connecting'")

    client.copy_from_host("key", "/root/.ssh/id_ed25519.setup")
    client.succeed("chmod 600 /root/.ssh/id_ed25519.setup")
    # Install the SSH key on the server.
    server.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
    server.succeed("systemctl restart sshd")

    client.succeed("NIX_SSHOPTS='-oStrictHostKeyChecking=no -i /root/.ssh/id_ed25519.setup' nix copy --to ssh://server ${pkgA}")
    server.succeed("nix-store --check-validity ${pkgA}")
    # Check that ControlMaster is working
    client.succeed("nix copy --to ssh://server ${pkgA}")

    client.succeed("cp /root/.ssh/id_ed25519.setup /root/.ssh/id_ed25519")

    client.succeed(f"ssh -o StrictHostKeyChecking=no {server.name} 'echo hello world' >&2")
    client.succeed(f"ssh -O check {server.name}")
    client.succeed(f"ssh -O exit {server.name}")
    client.fail(f"ssh -O check {server.name}")

    # Check that an explicit master will work
    client.succeed(f"ssh -MNfS /tmp/master {server.name}")
    client.succeed(f"ssh -S /tmp/master -O check {server.name}")
    client.succeed("NIX_SSHOPTS='-oControlPath=/tmp/master' nix copy --to ssh://server ${pkgA} >&2")
    client.succeed(f"ssh -S /tmp/master -O exit {server.name}")

    # Copy the closure of package B from the server to the client, using ssh-ng.
    client.fail("nix-store --check-validity ${pkgB}")
    # Shouldn't download untrusted paths by default
    client.fail("nix copy --from ssh-ng://server ${pkgB} >&2")
    client.succeed("nix copy --no-check-sigs --from ssh-ng://server ${pkgB} >&2")
    client.succeed("nix-store --check-validity ${pkgB}")

    # Copy the derivation of package D's derivation from the client to the server.
    server.fail("nix-store --check-validity ${pkgD.drvPath}")
    client.succeed("nix copy --derivation --to ssh://server ${pkgD.drvPath} >&2")
    server.succeed("nix-store --check-validity ${pkgD.drvPath}")
  '';
}
