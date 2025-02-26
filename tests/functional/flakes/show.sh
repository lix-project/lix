source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

writeSimpleFlake "$flakeDir"
cd "$flakeDir"


# FIXME(jade): the following is rather absurd. we have jq!

# By default: Only show the packages content for the current system and no
# legacyPackages at all
nix flake show --json > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.someOtherSystem.default == {};
assert show_output.packages.${builtins.currentSystem}.default.name == "simple";
assert show_output.legacyPackages.${builtins.currentSystem} == {};
true
'

# Follow --eval-system for determining the system for flakes
nix flake show --eval-system someOtherSystem --json > show-output.json
drvTitle=$(jq -r '.packages.someOtherSystem.default.name' show-output.json)
[[ $drvTitle == 'simple' ]]

# With `--all-systems`, show the packages for all systems
nix flake show --json --all-systems > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.someOtherSystem.default.name == "simple";
assert show_output.legacyPackages.${builtins.currentSystem} == {};
true
'

# With `--legacy`, show the legacy packages
nix flake show --json --legacy > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.${builtins.currentSystem}.hello.name == "simple";
true
'

# Test that attributes are only reported when they have actual content
cat >flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    apps.$system = { };
    checks.$system = { };
    devShells.$system = { };
    legacyPackages.$system = { };
    packages.$system = { };
    packages.someOtherSystem = { };

    formatter = { };
    nixosConfigurations = { };
    nixosModules = { };
  };
}
EOF
nix flake show --json --all-systems > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output == { };
true
'

# Test that attributes with errors are handled correctly.
# nixpkgs.legacyPackages is a particularly prominent instance of this.
cat >flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      AAAAAASomeThingsFailToEvaluate = throw "nooo";
      simple = import ./simple.nix;
    };
  };
}
EOF
nix flake show --json --legacy --all-systems > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.${builtins.currentSystem}.AAAAAASomeThingsFailToEvaluate == { };
assert show_output.legacyPackages.${builtins.currentSystem}.simple.name == "simple";
true
'

cat >flake.nix<<EOF
{
  outputs = inputs: {
    packages.$system = {
      aNoDescription = import ./simple.nix;
      bOneLineDescription = import ./simple.nix // { meta.description = "one line"; };
      cMultiLineDescription = import ./simple.nix // { meta.description = ''
         line one
        line two
      ''; };
      dLongDescription = import ./simple.nix // { meta.description = ''
        abcdefghijklmnopqrstuvwxyz
      ''; };
      eEmptyDescription = import ./simple.nix // { meta.description = ""; };
    };
  };
}
EOF

runinpty sh -c '
  stty rows 20 cols 100
  TERM=xterm-256color NOCOLOR=1 nix flake show
' > show-output.txt

test "$(awk -F '[:] ' '/aNoDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/bOneLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'one line'"
test "$(awk -F '[:] ' '/cMultiLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'line one'"
test "$(awk -F '[:] ' '/dLongDescription/{print $NF}' ./show-output.txt)" = "package 'simple' - 'abcdefghijklmnopqrstu…'"
test "$(awk -F '[:] ' '/eEmptyDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"

# validate that having a broken window size does not cause anything to explode
# and that descriptions are just not printed if there is no space at all
runinpty sh -c '
  stty rows 0 cols 0
  TERM=xterm-256color NOCOLOR=1 nix flake show
' > show-output.txt
test "$(awk -F '[:] ' '/aNoDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/bOneLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/cMultiLineDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/dLongDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
test "$(awk -F '[:] ' '/eEmptyDescription/{print $NF}' ./show-output.txt)" = "package 'simple'"
