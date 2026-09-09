requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR

# system-features will automatically be added to the outer URL, but not inner
# remote-store URL.
builders="$TEST_HOME/machines.conf"
cat >"$builders" <<EOF
ssh://localhost?remote-store=$TEST_ROOT/machine1?system-features=foo - - 1 1 foo
$TEST_ROOT/machine2 - - 1 1 bar
ssh-ng://localhost?remote-store=$TEST_ROOT/machine3?system-features=baz - - 1 1 baz
EOF

chmod -R +w $TEST_ROOT/machine* || true
rm -rf $TEST_ROOT/machine* || true

# Note: ssh://localhost bypasses ssh, directly invoking nix-store as a
# child process. This allows us to test LegacySSHStore::buildDerivation().
# ssh-ng://... likewise allows us to test RemoteStore::buildDerivation().
nix build -L -v -f $file -o $TEST_ROOT/result --max-jobs 0 \
  --arg busybox $busybox \
  --arg useCA $useCA \
  --store $TEST_ROOT/machine0 \
  --builders "@$builders"

outPath=$(readlink -f $TEST_ROOT/result)

grep 'FOO BAR BAZ' $TEST_ROOT/machine0/$outPath

testPrintOutPath=$(nix build -L -v -f $file --no-link --print-out-paths --max-jobs 0 \
  --arg busybox $busybox \
  --arg useCA $useCA \
  --store $TEST_ROOT/machine0 \
  --builders "@$builders"
)

[[ $testPrintOutPath =~ store.*build-remote ]]

# Ensure that input1 was built on store1 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine1 --all)
echo "$output" | grepQuiet builder-build-remote-input-1.sh
echo "$output" | grepQuietInverse builder-build-remote-input-2.sh
echo "$output" | grepQuietInverse builder-build-remote-input-3.sh
unset output

# Ensure that input2 was built on store2 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine2 --all)
echo "$output" | grepQuietInverse builder-build-remote-input-1.sh
echo "$output" | grepQuiet builder-build-remote-input-2.sh
echo "$output" | grepQuietInverse builder-build-remote-input-3.sh
unset output

# Ensure that input3 was built on store3 due to the required feature.
output=$(nix path-info --store $TEST_ROOT/machine3 --all)
echo "$output" | grepQuietInverse builder-build-remote-input-1.sh
echo "$output" | grepQuietInverse builder-build-remote-input-2.sh
echo "$output" | grepQuiet builder-build-remote-input-3.sh
unset output


for i in input1 input3; do
nix log --store $TEST_ROOT/machine0 --file "$file" --arg busybox $busybox --arg useCA $useCA passthru."$i" | grep hi-$i
done

# Behavior of keep-failed
out="$(nix-build 2>&1 failing.nix \
  --no-out-link \
  --builders "@$builders"  \
  --keep-failed \
  --store $TEST_ROOT/machine0 \
  -j0 \
  --arg busybox $busybox)" || true

[[ "$out" =~ .*"note: keeping build directory".* ]]

build_dir="$(grep "note: keeping build" <<< "$out" | sed -E "s/^(.*)note: keeping build directory '(.*)'(.*)$/\2/")"
[[ "foo" = $(<"$build_dir"/b/bar) ]]

# should work for ssh-ng too
tmp_builders="$TEST_HOME/machines2.conf"
sed -e 's/ssh:/ssh-ng:/g' <$builders >$tmp_builders
out="$(nix-build 2>&1 failing.nix \
  --no-out-link \
  --builders "@$tmp_builders"  \
  --keep-failed \
  --store $TEST_ROOT/machine0 \
  -j0 \
  --arg busybox $busybox)" || true

[[ "$out" =~ .*"note: keeping build directory".* ]]

build_dir="$(grep "note: keeping build" <<< "$out" | sed -E "s/^(.*)note: keeping build directory '(.*)'(.*)$/\2/")"
[[ "foo" = $(<"$build_dir"/b/bar) ]]

# regression fj#928: --keep-going doesn't keep going with remote builders
output="$(nix-build 2>&1 \
  --store $TEST_ROOT/machine0 \
  --builders "ssh-ng://localhost?remote-store=$TEST_ROOT/machine3 - - 1 1" \
  --keep-going \
  --max-jobs 0 \
  --expr '
    let
      fail = n: derivation {
        name = n;
        system = builtins.currentSystem;
        builder = "/bin/sh";
        args = [ "-c" "false" ];
      };
    in {
      a = fail "a";
      b = fail "b";
    }
  ' || true)"
grep 'a.drv. failed on remote builder' <<<"$output"
grep 'b.drv. failed on remote builder' <<<"$output"
