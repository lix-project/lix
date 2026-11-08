with import ./config.nix;

mkDerivation {
  name = "remote-log-lines";
  requiredSystemFeatures = [ "foo" ];
  buildCommand = ''
    seq 30 | sed 's/^/line-/'
    exit 1
  '';
}
