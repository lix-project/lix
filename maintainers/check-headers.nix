{ writeShellApplication, gnugrep }:
writeShellApplication {
  name = "check-headers";

  runtimeInputs = [ gnugrep ];
  text = builtins.readFile ./check-headers.sh;
}
