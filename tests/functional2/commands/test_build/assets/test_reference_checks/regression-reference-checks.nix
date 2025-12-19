with import ./config.nix;
mkDerivation {
  name = "test";
  __structuredAttrs = true;
  outputs = [ "out" "man" ];
  outputChecks.out.disallowedReferences = [ "man" ];
  buildCommand = ''
    source $NIX_ATTRS_SH_FILE
    mkdir ''${outputs[out]}
    mkdir ''${outputs[man]}
  '';
}
