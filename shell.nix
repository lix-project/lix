let
  lock = builtins.fromJSON (builtins.readFile ./flake.lock);
  shell =
    (import (fetchTarball {
      url = "https://github.com/edolstra/flake-compat/archive/${lock.nodes.flake-compat.locked.rev}.tar.gz";
      sha256 = lock.nodes.flake-compat.locked.narHash;
    }) { src = ./.; }).shellNix;
in
shell.default // shell.devShells.${builtins.currentSystem}
