with import ./config.nix;
mkDerivation {
  name = "log-lines-fail";
  buildCommand = ''
    seq 30 | sed 's/^/line-/'
    exit 1
  '';
}
