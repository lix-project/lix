{ nixpkgs, pkgs, ... }:

let
  nestedCgroupsExpr = config: pkgs.writeText "nested.nix" ''
    let utils = builtins.storePath ${config.system.build.extraUtils}; in
    derivation {
      name = "nested-cgroups";
      system = builtins.currentSystem;
      requiredSystemFeatures = [ "uid-range" ];
      PATH = "''${utils}/bin";
      builder = "''${utils}/bin/sh";
      args = [ "-c" "mount -t cgroup2 none /sys/fs/cgroup; mkdir -p /sys/fs/cgroup/demo; touch $out" ];
    }
  '';
in
{
  name = "cgroups";

  nodes =
    {
      host =
        { config, pkgs, ... }:
        { virtualisation.additionalPaths = [ pkgs.stdenvNoCC config.system.build.extraUtils ];
          virtualisation.writableStore = true;
          nix.extraOptions =
            ''
              extra-experimental-features = nix-command auto-allocate-uids cgroups
              auto-allocate-uids = true
              extra-system-features = uid-range
            '';
          nix.settings = {
            download-attempts = 1;
            use-cgroups = true;
          };
          nix.nixPath = [ "nixpkgs=${nixpkgs}" ];
        };
    };

  testScript = { nodes }: ''
    start_all()

    host.wait_for_unit("multi-user.target")

    # Start build in background
    host.execute("nix build --use-cgroups --auto-allocate-uids --file ${./hang.nix} >&2 &")
    pid = int(host.succeed("pgrep nix"))
    service = "/sys/fs/cgroup/system.slice/system-nix\\\\x2ddaemon.slice/nix-daemon@*.service"

    # Wait for cgroups to be created
    host.succeed(f"until [ -e {service}/supervisor ]; do sleep 1; done", timeout=30)
    host.succeed(f"until [ -e {service}/nix-build@* ]; do sleep 1; done", timeout=30)

    # Check that there aren't processes where there shouldn't be, and that there are where there should be
    host.succeed(f'[ -z "$(cat {service}/cgroup.procs)" ]')
    host.succeed(f'[ -n "$(cat {service}/supervisor/cgroup.procs)" ]')
    host.succeed(f'[ -n "$(cat {service}/nix-build@*/cgroup.procs)" ]')

    # Perform an interrupt
    host.execute(f"kill -SIGINT {pid}")

    # Check that there aren't any cgroups anymore, neither any state records
    host.succeed(f"until [ ! -e {service}/nix-build@* ]; do sleep 1; done", timeout=30)
    host.succeed("until [ ! -e /nix/var/nix/cgroups/nix-build@* ]; do sleep 1; done", timeout=30)

    # Check nested cgroup cleanup
    logs = host.succeed("nix-build ${nestedCgroupsExpr nodes.host} 2>&1 | tee /dev/stderr")
    assert "error: deleting cgroup" not in logs
  '';

}
