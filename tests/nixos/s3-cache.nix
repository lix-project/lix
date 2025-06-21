{
  lib,
  config,
  ...
}:

let
  garage-ephemeral-key = pkgs.callPackage ../../releng/garage-ephemeral-key {
    inherit (pkgs.writers) writePython3Bin;
  };

  pkgs = config.nodes.client.nixpkgs.pkgs;

  pkgA = pkgs.cowsay;
  pkgB = pkgs.hello;
in {
  name = "s3-cache";

  nodes.client = { pkgs, lib, ... }: {
    virtualisation.writableStore = true;
    virtualisation.additionalPaths = [ pkgA pkgB ];

    nix.settings.substituters = lib.mkForce [ ];
    nix.settings.experimental-features = [ "nix-command" ];

    environment.systemPackages = with pkgs; [
      awscli2
      brotli
    ];
  };

  nodes.s3 = { pkgs, ... }: {
    services.garage.enable = true;
    services.garage.package = pkgs.garage_1_x;
    services.garage.settings = {
      replication_factor = 1;

      rpc_bind_addr = "[::]:3901";
      rpc_secret = "4425f5c26c5e11581d3223904324dcb5b5d5dfb14e5e7f35e38c595424f5f1e6";

      s3_api.api_bind_addr = "[::]:3900";
      s3_api.s3_region = "garage";
      s3_api.root_domain = ".s3";

      admin = {
        api_bind_addr = "[::]:3903";
        admin_token = "UkLeGWEvHnXBqnueR3ISEMWpOnm40jH2tM2HnnL/0F4=";
      };
    };

    networking.firewall.allowedTCPPorts = [ 3900 ];

    environment.sessionVariables = {
      GARAGE_ADMIN_TOKEN = "UkLeGWEvHnXBqnueR3ISEMWpOnm40jH2tM2HnnL/0F4=";
    };

    environment.systemPackages = [
      pkgs.garage_1_x
      pkgs.git
      pkgs.build-release-notes
      pkgs.jq
      pkgs.nix-eval-jobs
      garage-ephemeral-key
    ];
  };

  testScript = ''
    import json
    import textwrap

    start_all()

    client.wait_for_unit("multi-user.target")
    s3.wait_for_unit("garage")
    s3.wait_for_open_port(3900)

    def run_test_packages(fail=False):
        fun = client.succeed if not fail else client.fail
        fun("${lib.getExe pkgA} <<<awoo >&2")
        fun("${lib.getExe pkgB} >&2")

    def setup_s3():
        nodeId = s3.succeed("garage node id")
        s3.succeed(f"garage layout assign -z dc1 -c 10G {nodeId}")
        s3.succeed("garage layout apply --version 1")
        s3.succeed("garage bucket create cache")

        out = json.loads(
            s3.succeed("garage-ephemeral-key new --name cache --read --write --age-sec 7200 cache")
        )

        aws_config = textwrap.dedent(f"""
        [default]
        endpoint_url = http://s3:3900
        aws_access_key_id = {out['id']}
        aws_secret_access_key = {out['secret_key']}
        region = garage
        """)

        with open("aws-credentials", "w") as f:
            f.write(aws_config)

        client.copy_from_host("aws-credentials", "/root/.aws/credentials")

    setup_s3()

    with subtest("Ensure that communication with S3 works"):
        t.assertEqual("", client.succeed("aws s3 ls s3://cache/").rstrip())

    STORE_URI = "s3://cache?write-nar-listing=1&ls-compression=br&compression=zstd&parallel-compression=true&region=garage&endpoint=s3:3900&scheme=http"

    with subtest("Copy packages to S3"):
        client.succeed(f"nix copy --to '{STORE_URI}' ${pkgA} ${pkgB}")

        # Cache isn't empty anymore.
        t.assertNotEqual("", client.succeed("aws s3 ls s3://cache/"))

    with subtest("Ensure narinfo, listing and nar exist"):
        for store_path in ["${baseNameOf pkgA}", "${baseNameOf pkgB}"]:
            hash_part = store_path.split("-")[0]

            client.succeed(f"aws s3 cp s3://cache/{hash_part}.narinfo .")

            narinfo = {}
            for line in client.succeed(f"cat {hash_part}.narinfo").strip().splitlines():
                key, value = line.strip().split(": ", 1)
                narinfo[key] = value

            t.assertEqual("zstd", narinfo["Compression"])
            t.assertEqual(f"/nix/store/{store_path}", narinfo["StorePath"])

            url = narinfo["URL"]
            client.succeed(f"aws s3 ls s3://cache/{url} >&2")

            client.succeed(f"aws s3 cp s3://cache/{hash_part}.ls .")
            listing = json.loads(client.succeed(f"cat {hash_part}.ls | brotli -d"))

            t.assertIn("root", listing)
            root = listing["root"]["entries"]
            t.assertIn("bin", root)
            t.assertEqual("directory", root["bin"]["type"])

    with subtest("Ensure that the path is substitutable"):
        run_test_packages()

        client.succeed("nix-store --delete ${pkgA}")
        client.succeed("nix-store --delete ${pkgB}")

        run_test_packages(fail=True)

        client.succeed(f"nix copy --from '{STORE_URI}' ${pkgA} ${pkgB} --no-check-sigs")

        run_test_packages()
  '';
}
