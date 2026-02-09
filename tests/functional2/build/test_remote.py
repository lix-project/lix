import pytest
import re
import textwrap
from urllib.parse import urlencode, quote
import sys

from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset
from testlib.fixtures.env import ManagedEnv


@pytest.fixture
def busybox_args(env: ManagedEnv) -> list[str]:
    return ["--arg", "busybox", env.path.which("busybox")]


@pytest.fixture(autouse=True)
def _setup_for_remote_builds(nix: Nix, env: ManagedEnv):
    # always add bash, otherwise lix can't execute the build hook
    env.path.add_program("bash")
    # we don't always use this feature, but it also doesn't hurt
    nix.settings.add_xp_feature("daemon-trust-override")


def _builders(proto: str, flags: list[str], env: ManagedEnv) -> str:
    prog = "nix-store" if proto == "ssh" else "nix-daemon"
    script = f"""\
        #!{sys.executable}
        import os, sys
        os.execvp("{env.dirs.nix_bin_dir}/nix", [*{[prog, *flags]!s}, *sys.argv[1:]])
    """
    path = env.dirs.test_root / "remote-builder" / "launch.py"
    path.parent.mkdir()
    path.write_text(textwrap.dedent(script))
    path.chmod(0o755)

    remote_store = "local?" + urlencode(
        {"system-features": "foo bar baz", "root": str(env.dirs.home / "remote")}, quote_via=quote
    )
    uri_args = urlencode(
        {"remote-program": str(path), "remote-store": remote_store}, quote_via=quote
    )
    return textwrap.dedent(f"""
        version = 1

        [machines.remote]
        uri = "{proto}://localhost?{uri_args}"
        jobs = 8
        speed-factor = 1
        supported-features = [ "foo", "bar", "baz" ]
    """)


@pytest.mark.full_sandbox
@with_files(
    {
        "build-hook.nix": get_global_asset("build-hook.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_unsigned(nix: Nix, env: ManagedEnv, busybox_args: list[str]):
    # We first build a dependency of the derivation we eventually want to build.
    nix.nix_build(
        [
            "build-hook.nix",
            "-A",
            "passthru.input2",
            *busybox_args,
            "--option",
            "system-features",
            "bar",
        ]
    ).run().ok()

    # Now when we go to build that downstream derivation, Lix will try to
    # copy our already-build `input2` to the remote store. That store object
    # is input-addressed, so this will fail.

    result = nix.nix_build(
        [
            "build-hook.nix",
            "--max-jobs",
            "0",
            *busybox_args,
            "--builders",
            _builders("ssh-ng", ["--force-untrusted"], env),
        ]
    ).run()
    result.expect(1)
    assert re.findall(
        r"cannot add path '[^ ]*' because it lacks a signature by a trusted key",
        result.stderr_plain,
    )


@pytest.mark.full_sandbox
@pytest.mark.parametrize(
    ("protocol", "flags"), [("ssh", []), ("ssh-ng", []), ("ssh-ng", ["--force-untrusted"])]
)
@with_files(
    {
        "build-hook.nix": get_global_asset("build-hook.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_ia(
    nix: Nix, env: ManagedEnv, busybox_args: list[str], protocol: str, flags: list[str]
):
    result = nix.nix_build(
        [
            "build-hook.nix",
            "--max-jobs",
            "0",
            *busybox_args,
            "--builders",
            _builders(protocol, flags, env),
        ]
    ).run()
    result.ok()

    out_path = (env.dirs.home / "result").readlink()
    assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"


@pytest.mark.full_sandbox
@pytest.mark.parametrize(("protocol", "flags"), [("ssh", []), ("ssh-ng", ["--force-untrusted"])])
@with_files(
    {
        "build-hook-ca-fixed.nix": get_global_asset("build-hook-ca-fixed.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_ca(
    nix: Nix, env: ManagedEnv, busybox_args: list[str], protocol: str, flags: list[str]
):
    # Remote doesn't trusts us, but this is fine because we are only
    # building (fixed) CA derivations.
    result = nix.nix_build(
        [
            "build-hook-ca-fixed.nix",
            "--max-jobs",
            "0",
            *busybox_args,
            "--builders",
            _builders(protocol, flags, env),
        ]
    ).run()
    result.ok()

    out_path = (env.dirs.home / "result").readlink()
    assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"
