{ hashInvalidator ? "" }:
with import ./config.nix;

let
  input0 = mkDerivation {
    name = "dependencies-input-0";
    buildCommand = "mkdir $out; echo foo > $out/bar";
  };

  input1 = mkDerivation {
    name = "dependencies-input-1";
    buildCommand = "mkdir $out; echo FOO > $out/foo";
  };

  input2 = mkDerivation {
    name = "dependencies-input-2";
    buildCommand = ''
      mkdir $out
      # Space-filler to test GC stats reporting
      head -c 100k /dev/zero > $out/filler
      echo BAR > $out/bar
      echo ${input0} > $out/input0
      echo ${input3} > $out/input3
    '';
  };

  input3 = mkDerivation {
    name = "dependencies-input-3";
    buildCommand = "mkdir $out; echo FOO > $out/foo";
  };

  fod_input = mkDerivation {
    name = "fod-input";
    buildCommand = ''
      echo ${hashInvalidator}
      echo FOD > $out
    '';
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = "1dq9p0hnm1y75q2x40fws5887bq1r840hzdxak0a9djbwvx0b16d";
  };

in
mkDerivation {
  name = "dependencies-top";
  builder = ./dependencies.builder0.sh + "/FOOBAR/../.";
  input1 = input1 + "/.";
  input2 = "${input2}/.";
  input3 = "${input3}/.";
  input1_drv = input1;
  input2_drv = input2;
  input3_drv = input3;
  input0_drv = input0;
  fod_input_drv = fod_input;
  meta.description = "Random test package";
}
