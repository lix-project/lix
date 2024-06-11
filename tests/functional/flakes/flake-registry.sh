source ./common.sh

# remove the flake registry from nix.conf, to set to default ("vendored")
sed -i '/flake-registry/d' "$NIX_CONF_DIR/nix.conf"

# Make sure the vendored registry contains the correct amount.
[[ $(nix registry list | wc -l) == 37 ]]
# sanity check, contains the important ones
nix registry list | grep '^global flake:nixpkgs'
nix registry list | grep '^global flake:home-manager'


# it should work the same if we set to vendored directly.
echo 'flake-registry = vendored' >> "$NIX_CONF_DIR/nix.conf"
[[ $(nix registry list | wc -l) == 37 ]]
# sanity check, contains the important ones
nix registry list | grep '^global flake:nixpkgs'
nix registry list | grep '^global flake:home-manager'


# the online flake registry should still work, but it is deprecated.
set -m
# port 0: auto pick a free port, unbufferred output
python3 -u -m http.server 0 --bind 127.0.0.1 > server.out &

# wait for the http server to admit it is working
while ! grep -qP 'port \d+' server.out ; do
  echo 'waiting for python http' >&2
  sleep 0.2
done

port=$(awk 'match($0,/port ([[:digit:]]+)/, ary) { print ary[1] }' server.out)

sed -i '/flake-registry/d' "$NIX_CONF_DIR/nix.conf"
echo "flake-registry = http://127.0.0.1:$port/flake-registry.json" >> "$NIX_CONF_DIR/nix.conf"
cat <<EOF > flake-registry.json
{
    "flakes": [
        {
        "from": {
            "type": "indirect",
            "id": "nixpkgs"
        },
        "to": {
            "type": "github",
            "owner": "NixOS",
            "repo": "nixpkgs"
        }
        },
        {
        "from": {
            "type": "indirect",
            "id": "private-flake"
        },
        "to": {
            "type": "github",
            "owner": "fancy-enterprise",
            "repo": "private-flake"
        }
        }
    ],
    "version": 2
}
EOF

[[ $(nix registry list | wc -l) == 2 ]]
nix registry list | grep '^global flake:nixpkgs'
nix registry list | grep '^global flake:private-flake'

# make sure we have a warning:
nix registry list 2>&1 | grep "config option flake-registry referring to a URL is deprecated and will be removed"

kill %python
