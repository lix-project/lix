from collections.abc import Callable
from testlib.fixtures.command import Command
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from testlib.fixtures.nix import Nix
import pytest

# FIXME: clear_store breaks daemons
pytestmark = pytest.mark.no_daemon


@with_files(get_global_asset_pack("dependencies"))
def test_export(nix: Nix):
    out_path = nix.nix_build(["dependencies.nix", "--no-out-link"]).run().ok().stdout_plain

    res = nix.nix_store(["--export", out_path]).run().ok()
    exported = res.stdout

    nix.clear_store()

    res = nix.nix_store(["--import"]).with_stdin(exported).run().expect(1)
    # importing a non-closure should fail
    assert "does not exist in the Lix database" in res.stderr_plain


@with_files(get_global_asset_pack("dependencies"))
def test_export_full(nix: Nix):
    out_path = nix.nix_build(["dependencies.nix", "--no-out-link"]).run().ok().stdout_plain

    paths = nix.nix_store(["-qR", out_path]).run().ok().stdout_plain.splitlines()
    res = nix.nix_store(["--export", *paths]).run().ok()
    exported = res.stdout

    nix.clear_store()

    res = nix.nix_store(["--import"]).with_stdin(exported).run().ok()

    assert set(res.stdout_plain.splitlines()) == set(paths)

    res = nix.nix_store(["--export", *paths]).run().ok()

    nix.clear_store()

    # Regression test: the derivers in exp_all2 are empty, which shouldn't
    # cause a failure.

    res = nix.nix_store(["--import"]).with_stdin(res.stdout).run().ok()
    assert set(res.stdout_plain.splitlines()) == set(paths)


@with_files(get_global_asset_pack("dependencies"))
def test_bad_fd(nix: Nix, command: Callable[[list[str]], Command]):
    out_path = nix.nix_build(["dependencies.nix", "--no-out-link"]).run().ok().stdout_plain
    # HACK(rootile, 2026-06): we can't pipe things into files from within our framework
    cmd = command(["bash", "-c", 'nix-store --export "$1" > /dev/full', "nix-store", out_path])
    res = cmd.run().expect(1)
    assert (
        "error: writing to file:" in res.stderr_plain
        or "Operation not permitted" in res.stderr_plain
    )
