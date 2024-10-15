{checkBuildId ? 0}:

with import ./config.nix;

{
  nondeterministic = mkDerivation {
    inherit checkBuildId;
    name = "nondeterministic";
    outputs = ["a" "b"];
    buildCommand =
      ''
        mkdir $a $b
        date +%s.%N > $a/date
        date +%s.%N > $b/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
      '';
  };

  deterministic = mkDerivation {
    inherit checkBuildId;
    name = "deterministic";
    outputs = ["a" "b"];
    buildCommand =
      ''
        mkdir $a $b
        echo date > $a/date
        echo date > $b/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
      '';
  };

  failed = mkDerivation {
    inherit checkBuildId;
    name = "failed";
    outputs = ["a" "b"];
    buildCommand =
      ''
        mkdir $a $b
        echo date > $a/date
        echo date > $b/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
        false
      '';
  };

  hashmismatch = import <nix/fetchurl.nix> {
    url = "file://" + builtins.getEnv "TEST_ROOT" + "/dummy";
    sha256 = "0mdqa9w1p6cmli6976v4wi0sw9r4p5prkj7lzfd1877wk11c9c73";
  };

  fetchurl = import <nix/fetchurl.nix> {
    url = "file://" + toString ./lang/eval-okay-xml.exp.xml;
    sha256 = "sha256-behBlX+DQK/Pjvkuc8Tx68Jwi4E5v86wDq+ZLaHyhQE=";
  };
}
