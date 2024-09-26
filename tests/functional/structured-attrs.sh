source common.sh

# 27ce722638 required some incompatible changes to the nix file, so skip this
# tests for the older versions
requireDaemonNewerThan "2.4pre20210712"

clearStore

rm -f $TEST_ROOT/result

nix-build structured-attrs.nix -A all -o $TEST_ROOT/result

[[ $(cat $TEST_ROOT/result/foo) = bar ]]
[[ $(cat $TEST_ROOT/result-dev/foo) = foo ]]
