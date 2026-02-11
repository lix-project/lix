import shutil
import pytest
import re
import dataclasses

from testlib.fixtures.command import CommandResult
from testlib.fixtures.nix import Nix
from testlib.fixtures.env import ManagedEnv


def build(nix: Nix, *args) -> CommandResult:
    expr = """
        derivation {
            name = "test";
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "echo > $out" ];
            outputHashMode = "flat";
            outputHash = "sha256-AbpHGcgLb+kRsJGnwFEktk7uzpZOCcBY74+YBdrKVGs=";
        }
    """
    return nix.nix_build(["-E", expr, "--no-link", "--no-require-sigs", *args]).run()


@dataclasses.dataclass
class Caches:
    good: str
    bad: str


@pytest.fixture
def caches(nix: Nix, env: ManagedEnv) -> Caches:
    good_path, bad_path = env.dirs.home / "good", env.dirs.home / "bad"
    good_uri, bad_uri = f"file://{good_path}", f"file://{bad_path}"

    # build the derivation
    output = build(nix).ok().stdout_s.strip()
    # copy it to the good cache
    nix.nix(["copy", output, "--to", good_uri, "--no-require-sigs"], flake=True).run().ok()
    # create the bad cache by simulating read failures
    shutil.copytree(good_path, bad_path)
    for info in bad_path.glob("*.narinfo"):
        info.chmod(0o200)

    nix.nix(["store", "delete", output], flake=True).run().ok()

    return Caches(good=good_uri, bad=bad_uri)


def test_substitution_fallback_good_first(nix: Nix, caches: Caches):
    build(nix, "--substituters", f"{caches.good} {caches.bad}").ok()


def test_substitution_fallback_bad_first(nix: Nix, caches: Caches):
    # we expect three warnings for the single nar: two from querying, one from the substitution itself
    result = build(nix, "--substituters", f"{caches.bad} {caches.good}").ok()
    assert len(re.findall(r"warning.*narinfo", result.stderr_s)) == 3


def test_substitution_fallback_may_build(nix: Nix, caches: Caches):
    # we expect two errors for the single nar: one from querying, one from the substitution itself
    result = build(nix, "--substituters", f"{caches.bad}", "--fallback").ok()
    assert len(re.findall(r"error.*narinfo", result.stderr_s)) == 2


def test_substitution_fallback_no_build(nix: Nix, caches: Caches):
    # we expect one error, and it's fatal
    result = build(nix, "--substituters", f"{caches.bad}").expect(1)
    assert len(re.findall(r"error.*narinfo", result.stderr_s)) == 1
