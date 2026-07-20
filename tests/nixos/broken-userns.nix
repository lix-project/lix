# Lix should be able to build derivations that want working NSS, even with
# broken user namespaces support
{ ... }:
let
  testDerivation = builtins.toFile "test.nix" ''
    { cacheBreak }:
    let pkgs = import <nixpkgs> { };
    in
    pkgs.runCommand "test" { } '''
      # ''${cacheBreak}
      id -g
      id -u
      echo "GROUP"
      cat /etc/group
      echo "PASSWD"
      cat /etc/passwd

      username=$(id -un)
      groupname=$(id -gn)
      [[ "$username" =~ nixbld* ]]
      [[ "$groupname" =~ nixbld* ]]
      touch $out
    '''
  '';
in
{
  name = "broken-userns";

  nodes.machine =
    {
      config,
      lib,
      pkgs,
      ...
    }:
    {
      virtualisation.writableStore = true;
      nix.settings.substituters = lib.mkForce [ ];
      nix.nixPath = [ "nixpkgs=${lib.cleanSource pkgs.path}" ];
      virtualisation.additionalPaths = [
        pkgs.stdenvNoCC
        testDerivation
      ];
    };

  testScript =
    { nodes }:
    ''
      start_all()

      # Building it normally should work
      machine.succeed(r"""
        nix-build --argstr cacheBreak 1 --store daemon ${testDerivation} --debug \
          |& tee /dev/stderr | grep -sv 'not using a user namespace'
      """)

      # Building it with broken userns should also work
      machine.succeed(r"""
        # break user ns
        sysctl -w user.max_user_namespaces=0
      """)
      # ensure we are running with socket activation. otherwise we would have to restart
      # the daemon for the sysctl to take full effect, but restarting the daemon is racy
      machine.succeed("systemctl status nix-daemon.socket")
      machine.fail("systemctl status nix-daemon.service")
      machine.succeed(r"""
        nix-build --argstr cacheBreak 2 --store daemon ${testDerivation} --debug \
          |& tee /dev/stderr | grep -s 'not using a user namespace'
      """)
    '';
}
