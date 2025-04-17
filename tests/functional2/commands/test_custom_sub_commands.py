import os
import stat
import sys

import pytest

from pathlib import Path

from textwrap import dedent
from functional2.testlib.fixtures import Nix

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
    command = request.param

    executable = custom_sub_command_path / f"lix-{command}"
    executable.write_text(dedent(f"""\
         #!{sys.executable}
         import os, sys
         # Start with args[0] set to the actual nix command used for testing
         # as we are not making Lix variants of those.
         os.execvp("nix-{command}", [ "nix-{command}" ] + sys.argv[1:])
         """))
    executable.chmod(stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)

    return command


@pytest.fixture(scope="module")
def failing_sub_command(failing_sub_command_path: Path, custom_sub_command: str) -> str:
    # Create an external command that will intentionally cause an error
    executable = failing_sub_command_path / f"lix-{custom_sub_command}"
    executable.write_text(dedent(f"""\
         #!{sys.executable}
         import sys
         sys.exit(42)
         """))
    executable.chmod(stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)

    return custom_sub_command


@pytest.fixture
def path(custom_sub_command_path: Path) -> str:
    # Provide a modified search path
    search_path = os.environ.get("PATH").split(":")
    search_path += ["/some/incorrect", "/location/for/fun", str(custom_sub_command_path)]

    return ":".join(search_path)


@pytest.fixture
def path_with_failure(request: pytest.FixtureRequest, custom_sub_command_path: Path,
                      failing_sub_command_path: Path) -> str:
    # Provide a search path with failing binaries inserted in the specified position
    (first, fail) = request.param
    search_path = os.environ.get("PATH").split(":")

    command_path = [str(custom_sub_command_path), str(failing_sub_command_path)]
    if fail:
        command_path.reverse()

    if first:
        search_path = command_path + search_path
    else:
        search_path += command_path

    return ":".join(search_path)


@pytest.mark.parametrize("nix_exe, flag, expected", [("nix", False, 1),
                                                     ("lix", False, 1),
                                                     ("nix", True, 1),
                                                     ("lix", True, 0)])
def test_sub_commands(nix: Nix, path: str, custom_sub_command: str, nix_exe: str,
                      flag: bool, expected: int):
    # Test custom sub commands in various configurations
    nix_command = nix.nix([custom_sub_command, "--version"], nix_exe=nix_exe)
    nix_command.with_env(PATH=path)
    if flag:
        nix_command.settings.feature("lix-custom-sub-commands")

    nix_command.run().expect(expected)


@pytest.mark.parametrize("path_with_failure, expected", [((True, True), 42),
                                                         ((True, False), 0),
                                                         ((False, True), 42),
                                                         ((False, False), 0)],
                         indirect=["path_with_failure"])
def test_sub_command_path_order(nix: Nix, path_with_failure: str, failing_sub_command: str,
                                expected: int):
    # Test handling of the order of the path for custom sub commands
    # Incidentally also tests passing through exit codes
    nix_command = nix.nix([failing_sub_command, "--version"], nix_exe="lix")
    nix_command.with_env(PATH=path_with_failure)
    nix_command.settings.feature("lix-custom-sub-commands")

    nix_command.run().expect(expected)


@pytest.mark.skip(reason="TODO: we do not support auto completion for custom sub commands for now")
def test_custom_sub_commands_auto_completion(nix: Nix, tmp_path: Path):
    pass


@pytest.mark.skip(reason="TODO: we do not test flag handling for custom sub commands for now")
def test_custom_sub_command_flag_handling(nix: Nix, tmp_path: Path):
    # TODO: Short, long and multiple flags should be tested as well.
    # TODO: `--` special flag
    # TODO: test positional arguments, but only `nix-copy-closure` implements some and it's pesky to test here.
    pass
