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
        "build-hook.nix": CopyFile("assets/build-hook.nix"),
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
    nix.nix_build(
        ["build-hook.nix", "-A", "passthru.input1", *busybox_args, "--arg", "useCA", "false"]
    ).run().ok()

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
                *["--arg", "useCA", "false"],
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
        "build-hook.nix": CopyFile("assets/build-hook.nix"),
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
            *["--arg", "useCA", "false"],
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
        "build-hook-ca-fixed.nix": CopyFile("assets/build-hook.nix"),
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
            *["--arg", "useCA", "true"],
        ]
    ).run()
    result.ok()

    out_path = (env.dirs.home / "result").readlink()
    assert nix.physical_store_path_for(out_path).read_text() == "FOO BAR BAZ\n"


@pytest.mark.full_sandbox
@with_files(
    {
        "build-hook-ca-fixed.nix": CopyFile("assets/build-hook.nix"),
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
                *["--arg", "useCA", "true"],
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


@pytest.mark.no_daemon
@pytest.mark.full_sandbox
def test_post_build_hook(nix: Nix):
    """
    test that remote builds call post-build hooks on the originating end of a remote build
    """

    hook_count = nix.env.dirs.home / "post-hook-counter"

    hook = nix.env.dirs.home / "hook.sh"
    hook.write_text(
        dedent(f"""\
            #!{nix.env.path.which("bash")}

            echo "Post hook ran successfully"
            # Add an empty line to a counter file, just to check that this hook ran properly
            echo "" >> {hook_count}
        """)
    )
    hook.chmod(0o755)

    nix.settings["post-build-hook"] = str(hook)

    expr = """
        derivation {
            name = "test";
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "echo foo > $out" ];
        }
    """

    nix.nix_build(
        [
            *["--builders", f"ssh-ng://localhost?remote-store={nix.env.dirs.home}/remote"],
            *["--keep-failed"],
            *["--max-jobs", "0"],
            *["--expr", expr],
        ]
    ).run().ok()

    # the hook will be called twice because the config is shared with the "remote" builder
    assert hook_count.read_text() == "\n\n"


@pytest.mark.full_sandbox
@pytest.mark.no_daemon
@with_files(
    {
        "build-hook.nix": CopyFile("assets/build-hook.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
@pytest.mark.parametrize("use_ca", ["false", "true"])
def test_feature_scheduling(nix: Nix, busybox_args: list[str], use_ca: str):
    # system-features will automatically be added to the outer URL, but not inner
    # remote-store URL.
    builders = nix.env.dirs.home / "machines.conf"
    builders.write_text(
        dedent(f"""
            ssh-ng://localhost?remote-store={nix.env.dirs.home}/machine1?system-features=foo - - 1 1 foo
            {nix.env.dirs.home}/machine2 - - 1 1 bar
            ssh-ng://localhost?remote-store={nix.env.dirs.home}/machine3?system-features=baz - - 1 1 baz
        """)
    )

    nix.settings.add_xp_feature("nix-command")
    nix.settings["builders"] = f"@{builders}"

    build_args = ["-f", "build-hook.nix", *busybox_args, "--arg", "useCA", use_ca]

    # Note: ssh-ng://localhost bypasses ssh, directly invoking nix-daemon as a
    # child process. This allows us to test RemoteStore::buildDerivation().
    result = nix.nix(["build", "-Lv", "-j0", *build_args, "--print-out-paths"]).run().ok()

    out_path = (nix.env.dirs.home / "result").readlink()
    assert out_path.read_text() == "FOO BAR BAZ\n"

    assert re.findall(r"store.*build-remote", result.stdout_plain)

    # Ensure that input1 was built on store1 due to the required feature.
    output = nix.nix(["path-info", "--store", f"{nix.env.dirs.home}/machine1", "--all"]).run().ok()
    assert "builder-build-remote-input-1.sh" in output.stdout_plain
    assert "builder-build-remote-input-2.sh" not in output.stdout_plain
    assert "builder-build-remote-input-3.sh" not in output.stdout_plain

    # Ensure that input2 was built on store2 due to the required feature.
    output = nix.nix(["path-info", "--store", f"{nix.env.dirs.home}/machine2", "--all"]).run().ok()
    assert "builder-build-remote-input-1.sh" not in output.stdout_plain
    assert "builder-build-remote-input-2.sh" in output.stdout_plain
    assert "builder-build-remote-input-3.sh" not in output.stdout_plain

    # Ensure that input3 was built on store3 due to the required feature.
    output = nix.nix(["path-info", "--store", f"{nix.env.dirs.home}/machine3", "--all"]).run().ok()
    assert "builder-build-remote-input-1.sh" not in output.stdout_plain
    assert "builder-build-remote-input-2.sh" not in output.stdout_plain
    assert "builder-build-remote-input-3.sh" in output.stdout_plain

    for i in ["input1", "input3"]:
        log = nix.nix(["log", *build_args, f"passthru.{i}"]).run().ok().stdout_plain
        assert f"hi-{i}" in log


@pytest.mark.no_daemon
@pytest.mark.full_sandbox
@pytest.mark.parametrize("scheme", ["ssh", "ssh-ng"])
def test_keep_failed(nix: Nix, scheme: str):
    expr = """
        derivation {
            name = "test";
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "echo foo > bar" ];
        }
    """

    result = (
        nix.nix_build(
            [
                *["--builders", f"{scheme}://localhost?remote-store={nix.env.dirs.home}/remote"],
                *["--keep-failed"],
                *["--max-jobs", "0"],
                *["--expr", expr],
            ]
        )
        .run()
        .expect(1)
    )
    assert "test.drv' failed on remote builder" in result.stderr_plain
    assert "keeping build directory" in result.stderr_plain
    assert next(iter(nix.env.dirs.nix_state_dir.glob("b/**/bar"))).read_text() == "foo\n"


@pytest.mark.no_daemon
def test_keep_going(nix: Nix):
    """
    regression fj#928: --keep-going doesn't keep going with remote builders
    """
    expr = """
        let
          fail = n: derivation {
            name = n;
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "false" ];
          };
        in {
          a = fail "a";
          b = fail "b";
        }
    """

    result = (
        nix.nix_build(
            [
                *["--builders", f"ssh-ng://localhost?remote-store={nix.env.dirs.home}/remote"],
                *["--keep-going"],
                *["--max-jobs", "0"],
                *["--expr", expr],
            ]
        )
        .run()
        .expect(1)
    )
    assert "a.drv' failed on remote builder" in result.stderr_plain
    assert "b.drv' failed on remote builder" in result.stderr_plain
