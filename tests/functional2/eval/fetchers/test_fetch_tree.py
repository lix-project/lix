from pathlib import Path
from testlib.fixtures.file_helper import File
from testlib.fixtures.file_helper import with_files
import pytest
from testlib.fixtures.nix import Nix


def fetch_tree_error(
    nix: Nix, arg: str = "fetchTree arg missing", msg: str = "messageSubstring missing"
):
    res = (
        nix.nix(
            ["eval", "--impure", "--raw", "--expr", f"(builtins.fetchTree {arg}).outPath"],
            flake=True,
        )
        .run()
        .expect(1)
    )
    assert msg in res.stderr_plain


@pytest.mark.parametrize("provider", ["gitlab", "github", "sourcehut"])
@pytest.mark.parametrize(
    ("arg", "msg"),
    [
        (
            '{ type = "@provider@"; owner = "foo"; repo = "bar"; ref = ",";}',
            "URL '@provider@:foo/bar' contains an invalid branch/tag name",
        ),
        (
            '"@provider@://host/foo/bar/,"',
            "URL '@provider@://host/foo/bar/,', ',' is not a commit hash or a branch/tag name",
        ),
        (
            '"@provider@://host/foo/bar/f16d8f43dd0998cdb315a2cccf2e4d10027e7ca4?rev=abc"',
            "URL '@provider@://host/foo/bar/f16d8f43dd0998cdb315a2cccf2e4d10027e7ca4?rev=abc' already contains a ref or rev",
        ),
        (
            '"@provider@://host/foo/bar/ref?ref=ref2"',
            "URL '@provider@://host/foo/bar/ref?ref=ref2' already contains a ref or rev",
        ),
        (
            '{ type = "@provider@"; owner = "foo"; repo = "bar"; host = "git_hub.com"; }',
            "URL '@provider@:foo/bar' contains an invalid instance host",
        ),
        (
            '"@provider@://host/foo/bar/ref?host=git_hub.com"',
            "URL '@provider@:foo/bar' contains an invalid instance host",
        ),
        (
            '{ type = "@provider@"; owner = "foo"; repo = "bar"; wrong = true; }',
            "unsupported input attribute 'wrong'",
        ),
        ('"@provider@://host/foo/bar/ref?wrong=1"', "unsupported input attribute 'wrong'"),
    ],
)
def test_provider_input_validation(nix: Nix, provider: str, arg: str, msg: str):

    fetch_tree_error(nix, arg.replace("@provider@", provider), msg.replace("@provider@", provider))


@pytest.mark.parametrize(
    ("arg", "msg"),
    [
        # tarball fetchers
        (
            '"https://host/foo?wrong=1"',
            'unsupported tarball input attribute \'wrong\'. If you wanted to fetch a tarball with a query parameter, please use \'{ type = "tarball"; url = "..."; }',
        ),
        # git fetchers
        (
            '"git+https://github.com/owner/repo?invalid=1"',
            "unsupported input attribute 'invalid' for the 'git' scheme",
        ),
        (
            '"git+https://github.com/owner/repo?url=foo"',
            "URL 'git+https://github.com/owner/repo?url=foo' must not override url via query param!",
        ),
        (
            '"git+https://github.com/owner/repo?ref=foo.lock"',
            "invalid Git branch/tag name 'foo.lock'",
        ),
        (
            '{ type = "git"; url ="https://github.com/owner/repo"; ref = "foo.lock"; }',
            "invalid Git branch/tag name 'foo.lock'",
        ),
        # mercurial
        (
            '"hg+https://forge.tld/owner/repo?invalid=1"',
            "unsupported input attribute 'invalid' for the 'hg' scheme",
        ),
        (
            '{ type = "hg"; url = "https://forge.tld/owner/repo"; invalid = 1; }',
            "unsupported input attribute 'invalid' for the 'hg' scheme",
        ),
        ('"hg+https://forge.tld/owner/repo?ref=,"', "invalid Mercurial branch/tag name ','"),
        (
            '{ type = "hg"; url = "https://forge.tld/owner/repo"; ref = ",";}',
            "invalid Mercurial branch/tag name ','",
        ),
    ],
)
def test_unsupported_attrs(nix: Nix, arg: str, msg: str):
    fetch_tree_error(nix, arg, msg)


@with_files({"testfile": File("hello lix")})
def test_fetch_tree_pure_eval(nix: Nix, files: Path):
    res = (
        nix.nix(
            [
                "eval",
                "--expr",
                f'(builtins.fetchTree {{ path = "{files}/testfile"; rev = "0000000000000000000000000000000000000000"; type = "path";}})',
            ],
            flake=True,
        )
        .run()
        .expect(1)
    )
    assert "error: in pure evaluation mode, 'fetchTree' requires a locked input" in res.stderr_plain
