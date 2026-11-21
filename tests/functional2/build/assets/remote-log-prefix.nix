with import ./config.nix;
mkDerivation {
  name = "remote-log-prefix";
  requiredSystemFeatures = [ "foo" ];
  buildCommand = ''
    echo hello-from-remote-builder
    mkdir -p $out
    echo done > $out/x
  '';
}
