source ./common.sh

requireGit

flakeFollowsA=$TEST_ROOT/follows/flakeA
flakeFollowsB=$TEST_ROOT/follows/flakeA/flakeB
flakeFollowsC=$TEST_ROOT/follows/flakeA/flakeB/flakeC
flakeFollowsD=$TEST_ROOT/follows/flakeA/flakeD
flakeFollowsE=$TEST_ROOT/follows/flakeA/flakeE

# Test following path flakerefs.
createGitRepo $flakeFollowsA
mkdir -p $flakeFollowsB
mkdir -p $flakeFollowsC
mkdir -p $flakeFollowsD
mkdir -p $flakeFollowsE

cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.foobar.follows = "foobar";
        };

        foobar.url = "path:$flakeFollowsA/flakeE";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsB/flake.nix <<EOF
{
    description = "Flake B";
    inputs = {
        foobar.url = "path:$flakeFollowsA/flakeE";
        goodoo.follows = "C/goodoo";
        C = {
            url = "path:./flakeC";
            inputs.foobar.follows = "foobar";
        };
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsC/flake.nix <<EOF
{
    description = "Flake C";
    inputs = {
        foobar.url = "path:$flakeFollowsA/flakeE";
        goodoo.follows = "foobar";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsD/flake.nix <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsE/flake.nix <<EOF
{
    description = "Flake E";
    inputs = {};
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeD/flake.nix flakeE/flake.nix

nix flake metadata $flakeFollowsA

nix flake update --flake $flakeFollowsA

nix flake lock $flakeFollowsA

oldLock="$(cat "$flakeFollowsA/flake.lock")"

# Ensure that locking twice doesn't change anything

nix flake lock $flakeFollowsA

newLock="$(cat "$flakeFollowsA/flake.lock")"

diff <(echo "$newLock") <(echo "$oldLock")

[[ $(jq -c .nodes.B.inputs.C $flakeFollowsA/flake.lock) = '"C"' ]]
[[ $(jq -c .nodes.B.inputs.foobar $flakeFollowsA/flake.lock) = '["foobar"]' ]]
[[ $(jq -c .nodes.C.inputs.foobar $flakeFollowsA/flake.lock) = '["B","foobar"]' ]]

# Ensure removing follows from flake.nix removes them from the lockfile

cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
        };
        D.url = "path:./flakeD";
    };
    outputs = { ... }: {};
}
EOF

nix flake lock $flakeFollowsA

[[ $(jq -c .nodes.B.inputs.foobar $flakeFollowsA/flake.lock) = '"foobar"' ]]
jq -r -c '.nodes | keys | .[]' $flakeFollowsA/flake.lock | grep "^foobar$"

# Ensure a relative path is not allowed to go outside the store path
cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B.url = "path:../flakeB";
    };
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix

expect 1 nix flake lock $flakeFollowsA 2>&1 | grep 'points outside'

# Non-existant follows should print a warning.
cat >$flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs.B = {
        url = "path:./flakeB";
        inputs.invalid.follows = "D";
        inputs.invalid2.url = "path:./flakeD";
    };
    inputs.D.url = "path:./flakeD";
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix

nix flake lock "$flakeFollowsA" 2>&1 | grep "warning: input 'B' has an override for a non-existent input 'invalid'"
nix flake lock "$flakeFollowsA" 2>&1 | grep "warning: input 'B' has an override for a non-existent input 'invalid2'"

# Now test follow path overloading
# This tests a lockfile checking regression https://github.com/NixOS/nix/pull/8819
#
# We construct the following graph, where p->q means p has input q.
# A double edge means that the edge gets overridden using `follows`.
#
#      A
#     / \
#    /   \
#   v     v
#   B ==> C   --- follows declared in A
#    \\  /
#     \\/     --- follows declared in B
#      v
#      D
#
# The message was
#    error: input 'B/D' follows a non-existent input 'B/C/D'
#
# Note that for `B` to resolve its follow for `D`, it needs `C/D`, for which it needs to resolve the follow on `C` first.
flakeFollowsOverloadA="$TEST_ROOT/follows/overload/flakeA"
flakeFollowsOverloadB="$TEST_ROOT/follows/overload/flakeA/flakeB"
flakeFollowsOverloadC="$TEST_ROOT/follows/overload/flakeA/flakeB/flakeC"
flakeFollowsOverloadD="$TEST_ROOT/follows/overload/flakeA/flakeB/flakeC/flakeD"

# Test following path flakerefs.
createGitRepo "$flakeFollowsOverloadA"
mkdir -p "$flakeFollowsOverloadB"
mkdir -p "$flakeFollowsOverloadC"
mkdir -p "$flakeFollowsOverloadD"

cat > "$flakeFollowsOverloadD/flake.nix" <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsOverloadC/flake.nix" <<EOF
{
    description = "Flake C";
    inputs.D.url = "path:./flakeD";
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsOverloadB/flake.nix" <<EOF
{
    description = "Flake B";
    inputs = {
        C = {
            url = "path:./flakeC";
        };
        D.follows = "C/D";
    };
    outputs = { ... }: {};
}
EOF

# input B/D should be able to be found...
cat > "$flakeFollowsOverloadA/flake.nix" <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.C.follows = "C";
        };
        C.url = "path:./flakeB/flakeC";
    };
    outputs = { ... }: {};
}
EOF

git -C "$flakeFollowsOverloadA" add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeB/flakeC/flakeD/flake.nix

nix flake metadata "$flakeFollowsOverloadA"
nix flake update --flake "$flakeFollowsOverloadA"
nix flake lock "$flakeFollowsOverloadA"

# Test nested flake overrides: A overrides B/C/D

cat <<EOF > $flakeFollowsD/flake.nix
{ outputs = _: {}; }
EOF
cat <<EOF > $flakeFollowsC/flake.nix
{
  inputs.D.url = "path:nosuchflake";
  outputs = _: {};
}
EOF
cat <<EOF > $flakeFollowsB/flake.nix
{
  inputs.C.url = "path:$flakeFollowsC";
  outputs = _: {};
}
EOF
cat <<EOF > $flakeFollowsA/flake.nix
{
  inputs.B.url = "path:$flakeFollowsB";
  inputs.D.url = "path:$flakeFollowsD";
  inputs.B.inputs.C.inputs.D.follows = "D";
  outputs = _: {};
}
EOF

nix flake lock $flakeFollowsA

[[ $(jq -c .nodes.C.inputs.D $flakeFollowsA/flake.lock) = '["D"]' ]]

# Test overlapping flake follows: B has D follow C/D, while A has B/C follow C

cat <<EOF > $flakeFollowsC/flake.nix
{
  inputs.D.url = "path:$flakeFollowsD";
  outputs = _: {};
}
EOF
cat <<EOF > $flakeFollowsB/flake.nix
{
  inputs.C.url = "path:nosuchflake";
  inputs.D.url = "path:nosuchflake";
  inputs.D.follows = "C/D";
  outputs = _: {};
}
EOF
cat <<EOF > $flakeFollowsA/flake.nix
{
  inputs.B.url = "path:$flakeFollowsB";
  inputs.C.url = "path:$flakeFollowsC";
  inputs.B.inputs.C.follows = "C";
  outputs = _: {};
}
EOF

# bug was not triggered without recreating the lockfile
nix flake update --flake $flakeFollowsA

[[ $(jq -c .nodes.B.inputs.D $flakeFollowsA/flake.lock) = '["B","C","D"]' ]]

cat <<EOF > $flakeFollowsA/flake.nix
{
  inputs.B.url = "path:$flakeFollowsB";
  inputs.C.url = "path:$flakeFollowsC";
  inputs.B.inputs.C.inputs.E.follows = "C";
    outputs = _: {};
}
EOF
cat <<EOF > $flakeFollowsB/flake.nix
{
  inputs.C.url = "path:$flakeFollowsC";
  outputs = _: {};
}
EOF

nix flake update --flake $flakeFollowsA 2>&1 | grepQuiet "warning: input 'B/C' has an override for a non-existent input 'E'"

# Test for Nested follows cause flake interactions to update the nested input #460
for letter in {A..E}; do
    path="flakeFollows${letter}"
    rm -f "${!path}"/flake.lock
done

cat <<EOF > $flakeFollowsA/flake.nix
{
  inputs = {
    B.url = "path:$flakeFollowsB";
    C = {
      url = "path:$flakeFollowsC";
      inputs.D.inputs.E.follows = "B";
    };
  };
  outputs = _: {};
}
EOF

cat <<EOF > $flakeFollowsB/flake.nix
{
  outputs = _: {};
}
EOF

cat <<EOF > $flakeFollowsC/flake.nix
{
  inputs = {
    D.url = "path:$flakeFollowsD";
  };
  outputs = _: {};
}
EOF

cat <<EOF > $flakeFollowsD/flake.nix
{
  inputs = {
    E.url = "path:$flakeFollowsE";
  };
  outputs = _: {};
}
EOF

# Lockfiles are cleared, initially the dependency needs to be fetched.
out="$(nix --verbose flake show path:$flakeFollowsA 2>&1)"
echo "$out"
[[ "$out" = *$'\n'"fetching path input 'path:"*"/flakeD'"$'\n'* ]]

# But on another flake command it doesn't.
out="$(nix --verbose flake show path:$flakeFollowsA 2>&1)"
[[ "$out" != *$'\n'"fetching path input 'path:"*"/flakeD'"$'\n'* ]]

# Make sure the nested override is actually correct in this testcase.
[[ "$(nix flake metadata path:$flakeFollowsA --json | jq '.locks.nodes.D.inputs.E|.[]' -r)" = "B" ]]
