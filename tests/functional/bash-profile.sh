source common.sh

sed -e "s|@localstatedir@|$TEST_ROOT/profile-var|g" < ../../scripts/nix-profile.sh.in > $TEST_ROOT/nix-profile.sh

user=$(whoami || echo -n nixbld)
rm -rf $TEST_HOME $TEST_ROOT/profile-var
mkdir -p $TEST_HOME
USER=$user $SHELL -e -c ". $TEST_ROOT/nix-profile.sh; set"
USER=$user $SHELL -e -c ". $TEST_ROOT/nix-profile.sh" # test idempotency
