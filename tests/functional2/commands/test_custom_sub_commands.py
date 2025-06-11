import stat
import sys

import pytest

from pathlib import Path

from textwrap import dedent

from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.fixtures.nix import Nix

COMMANDS = ["copy-closure", "collect-garbage"]


@pytest.fixture(scope="module")
def custom_sub_command_path(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return tmp_path_factory.mktemp(__name__)


@pytest.fixture(scope="module")
def failing_sub_command_path(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return tmp_path_factory.mktemp(__name__ + "-failing")


@pytest.fixture(scope="module", params=COMMANDS)
def custom_sub_command(request: pytest.FixtureRequest, custom_sub_command_path: Path) -> str:
    # Create an external command that is an alias of an existing one
    lix_cmd = request.param

    executable = custom_sub_command_path / f"lix-{lix_cmd}"
    executable.write_text(
        dedent(f"""\
         #!{sys.executable}
         import os, sys
         # Start with args[0] set to the actual nix command used for testing
         # as we are not making Lix variants of those.
         os.execvp("nix-{lix_cmd}", [ "nix-{lix_cmd}" ] + sys.argv[1:])
         """)
    )
    executable.chmod(stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)

    return lix_cmd


@pytest.fixture(scope="module")
def failing_sub_command(failing_sub_command_path: Path, custom_sub_command: str) -> str:
    # Create an external command that will intentionally cause an error
    executable = failing_sub_command_path / f"lix-{custom_sub_command}"
    executable.write_text(
        dedent(f"""\
         #!{sys.executable}
         import sys
         sys.exit(42)
         """)
    )
    executable.chmod(stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)

    return custom_sub_command


@pytest.fixture
def path(custom_sub_command_path: Path, env: ManagedEnv):
    """Provide a modified search path"""
    env.path.append("/some/incorrect")
    env.path.append("/location/for/fun")
    env.path.append(str(custom_sub_command_path))


@pytest.fixture
def path_with_failure(
    request: pytest.FixtureRequest,
    custom_sub_command_path: Path,
    failing_sub_command_path: Path,
    env: ManagedEnv,
):
    """
    Provide a search path with failing binaries inserted in the specified position
    """
    (first, fail) = request.param

    command_path = [str(custom_sub_command_path), str(failing_sub_command_path)]
    if fail:
        command_path.reverse()

    if first:
        env.path.prepend(command_path[1])
        env.path.prepend(command_path[0])
    else:
        env.path.append(command_path[0])
        env.path.append(command_path[1])


@pytest.mark.parametrize(
    ("nix_exe", "flag", "expected"),
    [("nix", False, 1), ("lix", False, 1), ("nix", True, 1), ("lix", True, 0)],
)
@pytest.mark.usefixtures("path")
def test_sub_commands(nix: Nix, custom_sub_command: str, nix_exe: str, flag: bool, expected: int):
    if flag:
        nix.settings.feature("lix-custom-sub-commands")

    # Test custom sub commands in various configurations
    nix.nix([custom_sub_command, "--version"], nix_exe=nix_exe).run().expect(expected)


@pytest.mark.parametrize(
    ("path_with_failure", "expected"),
    [((True, True), 42), ((True, False), 0), ((False, True), 42), ((False, False), 0)],
    indirect=["path_with_failure"],
)
@pytest.mark.usefixtures("path_with_failure")
def test_sub_command_path_order(nix: Nix, failing_sub_command: str, expected: int):
    # Test handling of the order of the path for custom sub commands
    # Incidentally also tests passing through exit codes
    nix.settings.feature("lix-custom-sub-commands")
    nix.nix([failing_sub_command, "--version"], nix_exe="lix").run().expect(expected)


@pytest.mark.skip(
    reason="TODO(raito): we do not support auto completion for custom sub commands for now"
)
def test_custom_sub_commands_auto_completion(nix: Nix, tmp_path: Path): ...


@pytest.mark.skip(
    reason="TODO(raito): we do not test flag handling for custom sub commands for now"
)
def test_custom_sub_command_flag_handling(nix: Nix, tmp_path: Path):
    # Short, long and multiple flags should be tested as well.
    # `--` special flag
    # test positional arguments, but only `nix-copy-closure` implements some, and it's pesky to test here.
    ...
