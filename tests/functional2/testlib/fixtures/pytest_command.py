import os
from pathlib import Path

import pytest
from _pytest.fixtures import FixtureRequest

from functional2.testlib.commands import Command


@pytest.fixture(name="pytest_command")
def _pytest_command(request: FixtureRequest, tmp_path: Path, do_snapshot_update: bool) -> Command:
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

    env = os.environ.copy()
    if propagate_update and do_snapshot_update:
        flags.append("--accept-tests")
    else:
        env.pop("_NIX_TEST_ACCEPT", None)
    return Command(
        argv=["pytest", "--basetemp", "../pytest_files", *flags], cwd=tmp_path / "functional2"
    ).with_env(**env)
