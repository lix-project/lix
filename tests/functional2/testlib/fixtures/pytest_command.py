import pytest
from _pytest.fixtures import FixtureRequest

from functional2.testlib.fixtures.command import Command
from functional2.testlib.fixtures.env import ManagedEnv


@pytest.fixture(name="pytest_command")
def _pytest_command(env: ManagedEnv, request: FixtureRequest, do_snapshot_update: bool) -> Command:
    """
    returns a preconfigured pytest command.
    The following things must be passed into the parametrization:
    A list of additional flags to pass to the command
    Optionally a boolean flag if snapshot updates should be propagated to the inside command
        i.e. if the --accept-tests flag is set, if it should also be set for the pytest command returned by this fixture
        True by default
    Parametrization examples: `[]`, `["-k", "lang"]`, `([], False)`, `(["--accept-tests"], False)`
    """
    params = request.param

    if len(params) == 2 and isinstance(params[-1], bool):
        flags, propagate_update = params
    else:
        flags = params
        propagate_update = True
    env.path.add_program("pytest")
    cwd = env.get_env("HOME") / "functional2"
    cmd = Command(argv=["pytest", "--basetemp", "../pytest_files", *flags], _env=env, cwd=cwd)
    if propagate_update and do_snapshot_update:
        flags.append("--accept-tests")
    return cmd
