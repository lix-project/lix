{ lix-overlay, ... }:
{ config, lib, ... }:
{
  options.lix.enableOverlay = lib.mkOption {
    type = lib.types.bool;
    description = "Whether to set nixpkgs' `nixVersions.stable` to Lix and apply compatibility overrides to packages known to require CppNix.";
    default = true;
  };
  config = lib.mkIf config.lix.enableOverlay {
    nixpkgs.overlays = [ lix-overlay ];
  };
}
