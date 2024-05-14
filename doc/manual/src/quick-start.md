# Quick Start

<div class="warning">

FIXME(Lix): This chapter is quite outdated with respect to recommended practices in 2024 and needs updating.
The commands in here will work, however, and the installation section is up to date.

For more updated guidance, see the links on <https://lix.systems/resources/>

</div>

This chapter is for impatient people who don't like reading
documentation.  For more in-depth information you are kindly referred
to subsequent chapters.

1. Install Lix:

   On Linux and macOS the easiest way to install Lix is to run the following shell command
   (as a user other than root):

   ```console
   $ curl -sSf -L https://install.lix.systems/lix | sh -s -- install
   ```

   For systems that **already have a Nix implementation installed**, such as NixOS systems, read our [install page](https://lix.systems/install)

   The install script will use `sudo`, so make sure you have sufficient rights.

   For other installation methods, see [here](installation/installation.md).

1. See what installable packages are currently available in the
   channel:

   ```console
   $ nix-env --query --available --attr-path
   nixpkgs.docbook_xml_dtd_43                    docbook-xml-4.3
   nixpkgs.docbook_xml_dtd_45                    docbook-xml-4.5
   nixpkgs.firefox                               firefox-33.0.2
   nixpkgs.hello                                 hello-2.9
   nixpkgs.libxslt                               libxslt-1.1.28
   …
   ```

1. Install some packages from the channel:

   ```console
   $ nix-env --install --attr nixpkgs.hello
   ```

   This should download pre-built packages; it should not build them
   locally (if it does, something went wrong).

1. Test that they work:

   ```console
   $ which hello
   /home/eelco/.nix-profile/bin/hello
   $ hello
   Hello, world!
   ```

1. Uninstall a package:

   ```console
   $ nix-env --uninstall hello
   ```

1. You can also test a package without installing it:

   ```console
   $ nix-shell --packages hello
   ```

   This builds or downloads GNU Hello and its dependencies, then drops
   you into a Bash shell where the `hello` command is present, all
   without affecting your normal environment:

   ```console
   [nix-shell:~]$ hello
   Hello, world!

   [nix-shell:~]$ exit

   $ hello
   hello: command not found
   ```

1. To keep up-to-date with the channel, do:

   ```console
   $ nix-channel --update nixpkgs
   $ nix-env --upgrade '*'
   ```

   The latter command will upgrade each installed package for which
   there is a “newer” version (as determined by comparing the version
   numbers).

1. If you're unhappy with the result of a `nix-env` action (e.g., an
   upgraded package turned out not to work properly), you can go back:

   ```console
   $ nix-env --rollback
   ```

1. You should periodically run the Lix garbage collector to get rid of
   unused packages, since uninstalls or upgrades don't actually delete
   them:

   ```console
   $ nix-collect-garbage --delete-old
   ```

   N.B. on NixOS there is an option [`nix.gc.automatic`](https://nixos.org/manual/nixos/stable/options#opt-nix.gc.automatic) to enable a systemd timer to automate this task.
