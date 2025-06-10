{
  self,
  system,
  pkgs,
  ...
}:
let
  releng = ./..;
  this-garage = pkgs.garage_1_x;
  garage-ephemeral-key = pkgs.callPackage ../garage-ephemeral-key {
    inherit (pkgs.writers) writePython3Bin;
  };

  build = self.release-jobs.all.build.${system}.doc;

  pythonPackages = (
    p: [
      p.yapf
      p.requests
      p.xdg-base-dirs
      p.packaging
      p.xonsh
    ]
  );
  pythonEnv = pkgs.python3.pythonOnBuildForHost.withPackages pythonPackages;
in
{
  name = "local-releng-manual-upload";

  nodes = {
    machine =
      { config, pkgs, ... }:
      {
        services.garage.enable = true;
        services.garage.package = this-garage;
        services.garage.settings = {
          replication_factor = 1;

          rpc_bind_addr = "[::]:3901";
          rpc_secret = "4425f5c26c5e11581d3223904324dcb5b5d5dfb14e5e7f35e38c595424f5f1e6";

          s3_api.api_bind_addr = "[::]:3900";
          s3_api.s3_region = "garage";
          s3_api.root_domain = ".localhost";

          admin = {
            api_bind_addr = "[::]:3903";
            admin_token = "UkLeGWEvHnXBqnueR3ISEMWpOnm40jH2tM2HnnL/0F4=";
          };
        };

        environment.systemPackages = [
          this-garage
          pkgs.git
          pkgs.build-release-notes
          pkgs.jq
          pkgs.nix-eval-jobs
          pkgs.awscli2
          pythonEnv
          garage-ephemeral-key
        ];

        environment.sessionVariables = {
          GARAGE_ADMIN_TOKEN = "UkLeGWEvHnXBqnueR3ISEMWpOnm40jH2tM2HnnL/0F4=";
        };

        # ≥ v6.12 kernel has a system wide corruption related to 9p. wait until
        # https://lore.kernel.org/all/w5ap2zcsatkx4dmakrkjmaexwh3mnmgc5vhavb2miaj6grrzat@7kzr5vlsrmh5/
        # resolves. once this is resolved and the fix lands in a stable kernel
        # in nixpkgs, this pin can be removed.
        boot.kernelPackages = pkgs.linuxPackages_6_6;
      };
  };
  testScript = ''
    machine.wait_for_unit("garage")
    machine.wait_for_open_port(3900)

    nodeId = machine.succeed("garage node id")
    machine.succeed(f"garage layout assign -z dc1 -c 10G {nodeId}")
    machine.succeed("garage layout apply --version 1")
    machine.succeed("garage bucket create local-docs")
    machine.succeed("garage bucket create local-cache")
    machine.succeed("garage bucket create local-releases")

    machine.succeed("git init --bare releng-target")

    machine.succeed("mkdir repo")
    machine.succeed("cd repo; git init --initial-branch release-2.92 .")
    machine.succeed("cd repo; git config user.email 'local@test.com'")
    machine.succeed("cd repo; git config user.name 'Test Releng'")

    machine.copy_from_host("${../../version.json}", "repo/version.json")
    machine.copy_from_host("${releng}", "repo/releng")
    machine.copy_from_host("${../../doc}", "repo/doc")
    machine.succeed("cd repo; git add .")
    machine.succeed("cd repo; git commit -m 'initial commit'")
    machine.succeed("cd repo; git switch -c releng/2.92.4")

    machine.succeed("cd repo; python3 -m releng prepare")
    machine.succeed("cd repo; python3 -m releng tag")
    machine.succeed("cd repo; mkdir -p release/manual")
    machine.succeed("cd repo; cp --no-preserve=mode -T -vr ${build}/share/doc/nix/manual ./release/manual")
    machine.succeed("cd repo; python3 -m releng upload --noconfirm --environment local --target manual")
  '';
}
