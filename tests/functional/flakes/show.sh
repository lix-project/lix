source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

writeSimpleFlake "$flakeDir"
cd "$flakeDir"


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
