source common.sh

clearStore

rm -f "$TEST_ROOT/result"

cat > "$TEST_ROOT/pre-hook.sh" <<-'EOF'
#!/bin/sh

# log the drvs that have been pre-hooked
echo "$1" >> "$(dirname "$0")"/drvs.txt
# sandbox: we have a sandbox path as second arg
if [[ $# == 2 ]]; then
    # FIXME: verify this is actually present inside the derivation builder
    # where sandbox is available.
    echo extra-sandbox-paths
    echo /foo=/bin/sh
    echo
else
    echo
fi
EOF

chmod +x "$TEST_ROOT/pre-hook.sh"
drvPath="$(nix eval --raw -f dependencies.nix drvPath)"
nix-store -r "$drvPath" --pre-build-hook "$TEST_ROOT/pre-hook.sh"

# We expect that the pre build hook got called on all the derivations in the closure we built
numDrvs="$(nix-store --query --requisites "$drvPath" | grep -c '\.drv$')"
[[ "$numDrvs" -gt 1 ]]
[[ "$numDrvs" == "$(< "$TEST_ROOT/drvs.txt" wc -l)" ]]
