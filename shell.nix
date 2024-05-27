let
  shell =
    (import (fetchTarball "https://github.com/edolstra/flake-compat/archive/master.tar.gz") {
      src = ./.;
    }).shellNix;
in
shell.default // shell.devShells.${builtins.currentSystem}
