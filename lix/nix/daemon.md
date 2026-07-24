R""(

# Examples

* Run the daemon:

  ```console
  # nix daemon
  ```

* Run the daemon and listen on standard I/O instead of binding to a UNIX socket:

  ```console
  # nix daemon --stdio
  ```

# Description

This command runs the Nix daemon, which is a required component in
multi-user Nix installations. It runs build tasks and other
operations on the Nix store on behalf of non-root users. Usually you
don't run the daemon directly; instead it's managed by a service
management framework such as `systemd` on Linux, or `launchctl` on Darwin.

Note that this daemon does not fork into the background.

)""
