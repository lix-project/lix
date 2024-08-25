source common.sh

drv='
builtins.derivation {
  name = "foo";
  builder = /bin/sh;
  system = builtins.currentSystem;
  requiredSystemFeatures = [ "glitter" ];
}
'

# -j0 without remote machines diagnoses build start failure
! out="$(nix-build 2>&1 -j0 --expr "$drv" \
    --builders '' \
    --system-features 'glitter')"
<<<"$out" grepQuiet 'error: unable to start any build; either set '\''--max-jobs'\'' to a non-zero value or enable remote builds.'

# -j0 with remote machines and missing features also diagnoses
! out="$(nix-build 2>&1 -j0 --expr "$drv" \
    --builders "ssh://localhost?remote-store=$TEST_ROOT/machine1" \
    --system-features 'glitter')"
<<<"$out" grepQuiet 'error: unable to start any build; remote machines may not have all required system features.'
