source common.sh

requireGit

clearStore

testFetchTreeError() {
    rawFetchTreeArg="${1?fetchTree arg missing}"
    messageSubstring="${2?messageSubstring missing}"

    output="$(nix eval --impure --raw --expr "(builtins.fetchTree $rawFetchTreeArg).outPath" 2>&1)" && status=0 || status=$?
    grepQuiet "$messageSubstring" <<<"$output"
    test "$status" -ne 0
}

# github/gitlab/sourcehut fetcher input validation
for provider in github gitlab sourcehut; do
    # ref/rev validation
    testFetchTreeError \
        "{ type = \"$provider\"; owner = \"foo\"; repo = \"bar\"; ref = \",\"; }" \
        "URL '$provider:foo/bar' contains an invalid branch/tag name"

    testFetchTreeError \
        "\"$provider://host/foo/bar/,\"" \
        "URL '$provider:foo/bar', ',' is not a commit hash or a branch/tag name"

    testFetchTreeError \
        "\"$provider://host/foo/bar/f16d8f43dd0998cdb315a2cccf2e4d10027e7ca4?rev=abc\"" \
        "URL '$provider://host/foo/bar/f16d8f43dd0998cdb315a2cccf2e4d10027e7ca4?rev=abc' already contains a ref or rev"

    testFetchTreeError \
        "\"$provider://host/foo/bar/ref?ref=ref2\"" \
        "URL '$provider://host/foo/bar/ref?ref=ref2' already contains a ref or rev"

    # host validation
    testFetchTreeError \
        "{ type = \"$provider\"; owner = \"foo\"; repo = \"bar\"; host = \"git_hub.com\"; }" \
        "URL '$provider:foo/bar' contains an invalid instance host"

    testFetchTreeError \
        "\"$provider://host/foo/bar/ref?host=git_hub.com\"" \
        "URL '$provider:foo/bar' contains an invalid instance host"

    # invalid attributes
    testFetchTreeError \
        "{ type = \"$provider\"; owner = \"foo\"; repo = \"bar\"; wrong = true; }" \
        "unsupported input attribute 'wrong'"

    testFetchTreeError \
        "\"$provider://host/foo/bar/ref?wrong=1\"" \
        "unsupported input attribute 'wrong'"
done

# unsupported attributes w/ tarball fetcher
testFetchTreeError \
    "\"https://host/foo?wrong=1\"" \
    "unsupported tarball input attribute 'wrong'. If you wanted to fetch a tarball with a query parameter, please use '{ type = \"tarball\"; url = \"...\"; }"

# test for unsupported attributes / validation in git fetcher
testFetchTreeError \
    "\"git+https://github.com/owner/repo?invalid=1\"" \
    "unsupported input attribute 'invalid' for the 'git' scheme"

testFetchTreeError \
    "\"git+https://github.com/owner/repo?url=foo\"" \
    "URL 'git+https://github.com/owner/repo?url=foo' must not override url via query param!"

testFetchTreeError \
    "\"git+https://github.com/owner/repo?ref=foo.lock\"" \
    "invalid Git branch/tag name 'foo.lock'"

testFetchTreeError \
    "{ type = \"git\"; url =\"https://github.com/owner/repo\"; ref = \"foo.lock\"; }" \
    "invalid Git branch/tag name 'foo.lock'"

# same for mercurial
testFetchTreeError \
    "\"hg+https://forge.tld/owner/repo?invalid=1\"" \
    "unsupported input attribute 'invalid' for the 'hg' scheme"

testFetchTreeError \
    "{ type = \"hg\"; url = \"https://forge.tld/owner/repo\"; invalid = 1; }" \
    "unsupported input attribute 'invalid' for the 'hg' scheme"

testFetchTreeError \
    "\"hg+https://forge.tld/owner/repo?ref=,\"" \
    "invalid Mercurial branch/tag name ','"

testFetchTreeError \
    "{ type = \"hg\"; url = \"https://forge.tld/owner/repo\"; ref = \",\"; }" \
    "invalid Mercurial branch/tag name ','"

echo 'hello lix' > testfile
output="$(nix eval --expr '(builtins.fetchTree { path = "'"$(pwd)"'/testfile"; rev = "0000000000000000000000000000000000000000"; type = "path"; })' 2>&1)" && status=0 || status=$?
[ "$status" -eq 1 ]
grepQuiet "error: in pure evaluation mode, 'fetchTree' requires a locked input" <<<"$output"
