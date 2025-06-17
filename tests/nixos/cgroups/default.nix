{ nixpkgs, ... }:

{
  name = "cgroups";

  nodes =
    {
      host =
        { config, pkgs, ... }:
        { virtualisation.additionalPaths = [ pkgs.stdenvNoCC ];
          nix.extraOptions =
            ''
              extra-experimental-features = nix-command auto-allocate-uids cgroups
              extra-system-features = uid-range
            '';
          nix.settings.use-cgroups = true;
          nix.nixPath = [ "nixpkgs=${nixpkgs}" ];
        };
    };

  testScript = { nodes }: ''
    start_all()

    host.wait_for_unit("multi-user.target")

    # Start build in background
    host.execute("nix build --use-cgroups --auto-allocate-uids --file ${./hang.nix} >&2 &")
    pid = int(host.succeed("pgrep nix"))
    service = "/sys/fs/cgroup/system.slice/nix-daemon.service"

    # Wait for cgroups to be created
    host.succeed(f"until [ -e {service}/supervisor ]; do sleep 1; done", timeout=30)
    host.succeed(f"until [ -e {service}/nix-build-uid-* ]; do sleep 1; done", timeout=30)

    # Check that there aren't processes where there shouldn't be, and that there are where there should be
    host.succeed(f'[ -z "$(cat {service}/cgroup.procs)" ]')
    host.succeed(f'[ -n "$(cat {service}/supervisor/cgroup.procs)" ]')
    host.succeed(f'[ -n "$(cat {service}/nix-build-uid-*/cgroup.procs)" ]')

    # Perform an interrupt
    host.execute(f"kill -SIGINT {pid}")

    # Check that there aren't any cgroups anymore, neither any state records
    host.succeed(f"until [ ! -e {service}/nix-build-uid-* ]; do sleep 1; done", timeout=30)
    host.succeed("until [ ! -e /nix/var/nix/cgroups/nix-build-uid-* ]; do sleep 1; done", timeout=30)
  '';

}
