/*
  NixOS-specific changes needed by Lix main go here.
  Changes should be upstreamed into nixpkgs before releasing.
*/

{ lix-overlay, ... }:
{
  imports = [
    (import ./base.nix { inherit lix-overlay; })
  ];
}
