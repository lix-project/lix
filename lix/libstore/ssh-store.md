R"(

**Store URL format**: `ssh-ng://[username@]hostname[?parameters...]`

Store type that allows full access to a Nix store on a remote machine.

Supported parameters:

  - `port=<u16>`: port to connect to on the remote host. defaults to `22`
  - `noninteractive=<0|1>`: if set to `1` SSH will not be allowed to run interactive password prompts. defaults to `0`, forcibly set to `1` for remote builders

)"
