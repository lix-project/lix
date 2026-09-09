/*
  NixOS-specific changes needed by Lix main go here.
  Changes should be upstreamed into nixpkgs before releasing.
*/

{ lix-overlay, ... }:
{ config, lib, ... }:

let
  cfg = config.lix;
in
{
  options.lix = {
    enableRpc = lib.mkEnableOption "experimental rpc";
  };

  config = lib.mkIf cfg.enableRpc {
    nix.settings.experimental-features = [ "rpc-sockets" ];
    systemd.sockets."lix-daemon-xp1".wantedBy = [ "sockets.target" ];
    environment.sessionVariables.NIX_REMOTE = "daemon?protocol=any";
  };

  imports = [
    (import ./base.nix { inherit lix-overlay; })
  ];
}
