# This tests checks that CA works in builder's environments, *with* a sandbox.
source common.sh

requireSandboxSupport
needLocalStore "the sandbox only runs on the builder side, so it makes no sense to test it with the daemon"

clearStore

# Note: we need to bind-mount $SHELL into the chroot. Currently we
# only support the case where $SHELL is in the Nix store, because
# otherwise things get complicated (e.g. if it's in /bin, do we need
# /lib as well?).
if [[ ! $SHELL =~ /nix/store ]]; then skipTest "Shell is not from Nix store"; fi
# An alias to automatically bind-mount the $SHELL on nix-build invocations
nix-sandbox-build () { nix-build --no-out-link --sandbox-paths /nix/store "$@"; }

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

export NIX_STORE_DIR=/my/store
export NIX_REMOTE=$TEST_ROOT/store0

## Test mounting of SSL certificates into the sandbox
testCert () {
    expectation=$1 # "missing" | "present"
    mode=$2        # "normal" | "fixed-output" | "clobbering-impurities"
    certFile=$3    # a string that can be the path to a cert file
    # `100` means build failure without extra info, see doc/manual/src/command-ref/status-build-failure.md
    ([ "$mode" == fixed-output ] || [ "$mode" == clobbering-impurities ]) && ret=1 || ret=100
    expectStderr $ret nix-sandbox-build cert-test.nix --argstr mode "$mode" --option ssl-cert-file "$certFile" |
        grepQuiet "CERT_${expectation}_IN_SANDBOX"
}
testCertWithoutOption () {
    expectation=$1 # "missing" | "present"
    mode=$2        # "normal" | "fixed-output" | "clobbering-impurities"
    # `100` means build failure without extra info, see doc/manual/src/command-ref/status-build-failure.md
    ([ "$mode" == fixed-output ] || [ "$mode" == clobbering-impurities ]) && ret=1 || ret=100
    expectStderr $ret nix-sandbox-build cert-test.nix --argstr mode "$mode" |
        grepQuiet "CERT_${expectation}_IN_SANDBOX"
}

nocert=$TEST_ROOT/no-cert-file.pem
cert=$TEST_ROOT/some-cert-file.pem
certsymlink=$TEST_ROOT/cert-symlink.pem
echo -n "CERT_CONTENT" > $cert
ln -s $cert $certsymlink

# No cert in sandbox when not a fixed-output derivation
testCert missing normal       "$cert"

# No cert in sandbox when ssl-cert-file is empty
testCert missing fixed-output ""

# No cert in sandbox when ssl-cert-file is a nonexistent file
testCert missing fixed-output "$nocert"

# Cert in sandbox when ssl-cert-file is set to an existing file
testCert present fixed-output "$cert"
testCert present-env-var fixed-output "$cert"
NIX_SSL_CERT_FILE="$cert" testCertWithoutOption present-env-var fixed-output

# Cert in sandbox when ssl-cert-file is set to a symlink
testCert present fixed-output "$certsymlink"
testCert present-env-var fixed-output "$certsymlink"
NIX_SSL_CERT_FILE="$certsymlink" testCertWithoutOption present-env-var fixed-output

# Cert in sandbox when ssl-cert-file is set to a file and impurities clobbers the environment variables.
testCert present clobbering-impurities "$cert"
testCert present-env-var clobbering-impurities "$cert"
# Set the environment variable to clobber it.
NIX_SSL_CERT_FILE=/nowhere testCert present-env-var clobbering-impurities "$cert"
NIX_SSL_CERT_FILE="$cert" testCertWithoutOption present-env-var clobbering-impurities
