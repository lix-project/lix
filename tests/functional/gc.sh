source common.sh

clearStore

createAndRootPaths() {
  drvPath=$(nix-instantiate dependencies.nix)
  outPath=$(nix-store -rvv "$drvPath")

  # Set a GC root.
  rm -f "$NIX_STATE_DIR"/gcroots/foo
  ln -sf $outPath "$NIX_STATE_DIR"/gcroots/foo
}

createAndRootPaths

[ "$(nix-store -q --roots $outPath)" = "$NIX_STATE_DIR/gcroots/foo -> $outPath" ]

nix-store --gc --print-roots | grep $outPath
nix-store --gc --print-live | grep $outPath
nix-store --gc --print-dead | grep $drvPath
if nix-store --gc --print-dead | grep -E $outPath$; then false; fi

nix-store --gc --print-dead

input2=$(readLink $outPath/reference-to-input-2)
if nix-store --delete $input2; then false; fi
test -e $input2

if nix-store --delete $outPath; then false; fi
test -e $outPath

for i in $NIX_STORE_DIR/*; do
    if [[ $i =~ /trash ]]; then continue; fi # compat with old daemon
    touch $i.lock
    touch $i.chroot
done

nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat $outPath/foobar
cat $outPath/reference-to-input-2/bar

# Check that the derivation has been GC'd.
if test -e $drvPath; then false; fi

rm "$NIX_STATE_DIR"/gcroots/foo

# Deleting the dependency should now be possible, and delete the referrer as well
nix-store --delete $input2
! test -e $outPath
! test -e $input2

createAndRootPaths
rm "$NIX_STATE_DIR"/gcroots/foo
nix-collect-garbage

# Check that the store is empty.
rmdir $NIX_STORE_DIR/.links
rmdir $NIX_STORE_DIR

createAndRootPaths
input0=$(< $outPath/reference-to-input-2/input0)
input2=$(readLink $outPath/reference-to-input-2)
fodDrv=$(nix-store -qR $drvPath | grep fod-input.drv)
fodOut=$(nix-store -q --outputs $fodDrv)
# path is still live but command should still succeed because of --skip-live
nix-store --delete --skip-live $(readLink $outPath/reference-to-input-2)

rm "$NIX_STATE_DIR"/gcroots/foo
# with the dependent unrooted, we should be able to remove input2...
nix-store --delete --delete-closure $input2 > delete-output
# which should remove input0, since only input2 and top depended on it and we passed --delete-closure
! test -e $input0
# but fod should be unaffected, since it's not part of input-2's closure
test -e $fodOut
# and stats should be reported correctly
grep "0.10 MiB freed$" delete-output
test -z "$(grep "0 paths deleted" delete-output)"


nix-collect-garbage
