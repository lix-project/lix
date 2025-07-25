source common.sh

if [[ $(uname) == Darwin ]]; then skipTest "Darwin does not have diverted stores"; fi
needLocalStore "--repair needs a local store"

clearStore

path=$(nix-build dependencies.nix -o $TEST_ROOT/result)
path2=$(nix-store -qR $path | grep input-2)
path2_basename=$(basename $path2)

# Corrupt a path in a *chroot* store and check whether nix-build --repair can fix it.
extra_chroot_store_path="$TEST_ROOT/extra-chroot-store"
# Required because otherwise storeDir=/build/... in the sandbox.
special_sandbox_build_dir="/build-tmp"
chroot_path=$(nix-build dependencies.nix -o $TEST_ROOT/chroot-result --sandbox-build-dir "$special_sandbox_build_dir" --store "$extra_chroot_store_path" --extra-sandbox-paths /nix/store)
chroot_path2="$extra_chroot_store_path/nix/store/$path2_basename"

[ -d $chroot_path2 ] || fail "chroot path $chroot_path2 does not exist"

nix-store --verify --check-contents --sandbox-build-dir "$special_sandbox_build_dir" -v --store "$extra_chroot_store_path" |& grepQuiet "$extra_chroot_store_path" || fail "$extra_chroot_store_path does not occur in the store verification, the chroot store parameter is ignored."
chroot_hash=$(nix-hash $path2)

chmod u+w $chroot_path2
touch $chroot_path2/bad

! nix-store --verify --check-contents -v --store "$extra_chroot_store_path" --sandbox-build-dir "$special_sandbox_build_dir"

# The path can be repaired by rebuilding the derivation.
nix-store --verify --check-contents --repair --store "$extra_chroot_store_path" --extra-sandbox-paths /nix/store --sandbox-build-dir "$special_sandbox_build_dir"

! [ -e $chroot_path2/bad ]
! [ -w $chroot_path2 ]

# NOTE: verify path must be given the *LOGICAL* path here. And not the physical path. Confusing? I know.
nix-store --verify-path $path2 --store "$extra_chroot_store_path"
