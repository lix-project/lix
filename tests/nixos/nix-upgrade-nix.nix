{ lib, config, ... }:

/**
 * Test that nix upgrade-nix works regardless of whether /nix/var/nix/profiles/default
 * is a nix-env style profile or a nix profile style profile.
 */

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  lix = pkgs.nix;
  lixVersion = lib.getVersion lix;

  newNix = pkgs.nixVersions.latest;
  newNixVersion = lib.getVersion newNix;

in {
  name = "nix-upgrade-nix";

  nodes = {
    machine = { config, lib, pkgs, ... }: {
      virtualisation.writableStore = true;
      virtualisation.additionalPaths = [ pkgs.hello.drvPath newNix ];
      nix.settings.substituters = lib.mkForce [ ];
      nix.settings.experimental-features = [ "nix-command" "flakes" ];
      services.getty.autologinUser = "root";

    };
  };

  testScript = { nodes }: ''
    # fmt: off

    start_all()

    machine.succeed("nix --version >&2")

    # Use Lix to install CppNix into the default profile, overriding /run/current-system/sw/bin/nix
    machine.succeed("nix-env --install '${lib.getBin newNix}' --profile /nix/var/nix/profiles/default")

    # Make sure that correctly got inserted into our PATH.
    default_profile_nix_path = machine.succeed("command -v nix")
    print(default_profile_nix_path)
    assert default_profile_nix_path.strip() == "/nix/var/nix/profiles/default/bin/nix", \
      f"{default_profile_nix_path.strip()=} != /nix/var/nix/profiles/default/bin/nix"

    # And that it's the Nix we specified
    default_profile_version = machine.succeed("nix --version")
    assert "${newNixVersion}" in default_profile_version, f"${newNixVersion} not in {default_profile_version}"

    # Now upgrade to Lix, and make sure that worked.
    machine.succeed("${lib.getExe lix} upgrade-nix --debug --store-path ${lix} 2>&1")
    default_profile_version = machine.succeed("nix --version")
    print(default_profile_version)
    assert "${lixVersion}" in default_profile_version, f"${lixVersion} not in {default_profile_version}"

    # Now 'break' this profile -- use nix profile on it so nix-env will no longer work on it.
    machine.succeed(
      "nix profile install --profile /nix/var/nix/profiles/default '${pkgs.hello.drvPath}^*' >&2"
    )

    # Confirm that nix-env is broken.
    machine.fail(
      "nix-env --query --installed --profile /nix/var/nix/profiles/default >&2"
    )

    # And use nix upgrade-nix one more time, on the `nix profile` style profile.
    # (Specifying Lix by full path so we can use --store-path.)
    machine.succeed(
      "${lib.getBin lix}/bin/nix upgrade-nix --store-path '${lix}' >&2"
    )

    default_profile_version = machine.succeed("nix --version")
    print(default_profile_version)
    assert "${lixVersion}" in default_profile_version, f"${lixVersion} not in {default_profile_version}"
  '';

}
