from testlib.fixtures.file_helper import CopyFile
from testlib.fixtures.file_helper import File
from pathlib import Path
from textwrap import dedent
import pytest
import re
import textwrap
from urllib.parse import urlencode, quote
import sys

from testlib.fixtures.nix import Nix, NixDaemon
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset
from testlib.fixtures.env import ManagedEnv


@pytest.fixture
def busybox_args(env: ManagedEnv) -> list[str]:
    return ["--arg", "busybox", env.path.which("busybox")]


@pytest.fixture(autouse=True)
def _setup_for_remote_builds(env: ManagedEnv):
    # always add bash, otherwise lix can't execute the build hook
    env.path.add_program("bash")


def _builders(proto: str, untrusted: bool, env: ManagedEnv) -> str:
    prog = "nix-store" if proto == "ssh" else "nix-daemon"
    script = f"""\
        #!{sys.executable}
        import os, sys
        {'os.environ["NIX_CONFIG"] += "\\ntrusted-users = \\nstore = /dev/null"' if untrusted else ""}
        os.execvp("{env.dirs.nix_bin_dir}/nix", ["{prog}", *sys.argv[1:]])
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
def test_remote_trustless_unsigned(
    nix: Nix, daemon: NixDaemon, env: ManagedEnv, busybox_args: list[str]
):
    nix.settings.trusted_users = "*"
    nix.settings.system_features = ["foo"]
    nix.settings.store = str(env.dirs.home / "peer")
    # We first build a dependency of the derivation we eventually want to build.
    nix.nix_build(["build-hook.nix", "-A", "passthru.input1", *busybox_args]).run().ok()

    # Now when we go to build that downstream derivation, Lix will try to
    # copy our already-build `input2` to the remote store. That store object
    # is input-addressed, so this will fail.

    with daemon(nix, settings={"system-features": "foo bar baz"}) as inner:
        result = nix.nix_build(
            [
                "build-hook.nix",
                "--max-jobs",
                "0",
                *busybox_args,
                "--builders",
                f"{inner.settings.store} - - - - foo,bar,baz",
            ]
        ).run()
    result.expect(1)
    assert re.findall(
        r"cannot add path '[^ ]*' because it lacks a signature by a trusted key",
        result.stderr_plain,
    )


@pytest.mark.nix_settings(trusted_users="*")
@pytest.mark.full_sandbox
@pytest.mark.parametrize(
    ("protocol", "untrusted"), [("ssh", False), ("ssh-ng", False), ("ssh-ng", True)]
)
@with_files(
    {
        "build-hook.nix": get_global_asset("build-hook.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_ia(
    nix: Nix, env: ManagedEnv, busybox_args: list[str], protocol: str, untrusted: bool
):
    result = nix.nix_build(
        [
            "build-hook.nix",
            "--max-jobs",
            "0",
            *busybox_args,
            "--builders",
            _builders(protocol, untrusted, env),
        ]
    ).run()
    result.ok()

    out_path = (env.dirs.home / "result").readlink()
    assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"


@pytest.mark.nix_settings(trusted_users="*")
@pytest.mark.full_sandbox
@pytest.mark.parametrize(("protocol", "untrusted"), [("ssh", False), ("ssh-ng", True)])
@with_files(
    {
        "build-hook-ca-fixed.nix": get_global_asset("build-hook-ca-fixed.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_ca(
    nix: Nix, env: ManagedEnv, busybox_args: list[str], protocol: str, untrusted: bool
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
            _builders(protocol, untrusted, env),
        ]
    ).run()
    result.ok()

    out_path = (env.dirs.home / "result").readlink()
    assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"


@pytest.mark.full_sandbox
@with_files(
    {
        "build-hook-ca-fixed.nix": get_global_asset("build-hook-ca-fixed.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_remote_trustless_ca_daemon(
    nix: Nix, daemon: NixDaemon, env: ManagedEnv, busybox_args: list[str]
):
    """
    Tests Store::buildDerivation
    """
    with daemon(nix, settings={"trusted-users": "*", "system-features": "foo bar baz"}) as inner:
        result = nix.nix_build(
            [
                *["--store", f"{nix.env.dirs.home}/store"],
                "build-hook-ca-fixed.nix",
                *["--max-jobs", "0"],
                *busybox_args,
                *["--builders", f"daemon?protocol={inner.daemon_protocol} - - - - foo,bar,baz"],
            ]
        ).run()
        result.ok()

        out_path = (env.dirs.home / "result").readlink()
        assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"


@with_files(
    {
        "check-reqs.nix": CopyFile("assets/check-reqs.nix"),
        "config.nix": get_global_asset("config.nix"),
        "builders.toml": File(
            dedent("""
                    [machines.fox]
                    uri = "file://test-home/fox-store"
                    supported-features = ["kvm", "big", "benchmark"]

                    [machines.dragon]
                    uri = "file:///dev/null/"
                    supported-features = ["kvm", "big", "benchmark"]

                    [machines.plushie]
                    uri = "ssh-ng://plushie@example.com"

                """)
        ),
    }
)
def test_logging_uses_machine_name(nix: Nix, files: Path):
    nix.settings["builders"] = f"@{files}/builders.toml"
    nix.settings["max-jobs"] = 0

    res = nix.nix_build(["check-reqs.nix", "-vvvvv"]).run().expect(1)
    for builder in ["fox", "dragon", "plushie"]:
        assert f"considering building on remote machine '{builder}'" in res.stderr_plain
        assert f"cannot build on '{builder}': error: " in res.stderr_plain
        assert f"connecting to '{builder}'..." in res.stderr_plain
