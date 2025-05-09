{
  lib,
  writePython3Bin,
  python3Packages,
}:

let
  package = writePython3Bin "garage-ephemeral-key" { libraries = [ python3Packages.requests ]; } (
    builtins.readFile ./garage-ephemeral-key.py
  );
in
package
