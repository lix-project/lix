import os
from collections.abc import Callable
from logging import Logger
from pathlib import Path
from typing import Any
from collections.abc import Generator

import pytest
from _pytest.config import Config
from _pytest.fixtures import FixtureRequest


def pytest_addoption(parser: pytest.Parser) -> None:
    """
    Adds an --accept-tests flag to pytest, which updates .exp files handled by snapshots
    :param parser: pytest parser, which handles the arguments
    """
    group = parser.getgroup("snapshots")
    group.addoption(
        "--accept-tests",
        action="store_true",
        help="if expected output should be updated",
        dest="accept-tests",
    )


@pytest.fixture(scope="session")
def do_snapshot_update(request: FixtureRequest) -> bool:
    return request.config.getoption("accept-tests") or (
        os.environ.get("_NIX_TEST_ACCEPT") is not None
    )


class Snapshot:
    def __init__(self, expected_output_path: Path, do_update: bool, logger: Logger):
        """
        A snapshot keeps track of the expected output within the given file.
        If the given file does not exist, an empty string will be assumed as the expected output
        If the `--accept-tests` flag is set or (for legacy reasons) the `_NIX_TEST_ACCEPT` environment variable is set
        The expected output will be updated with the current actual output
        The snapshot must be the **left hand operator** of the equal check.
        Example usage: `snapshot("eval_out.exp") == actual_out`
        :param expected_output_path: where the expected output file is located
        :param do_update: if the output should be updated
        :param logger: logger for debugging stuff, provided by pytest
        """
        self.expected_output_path = expected_output_path
        # assume empty string, if there is no expected output file (yet)
        self.content = expected_output_path.read_text() if expected_output_path.exists() else ""
        self.do_update = do_update
        self.logger = logger
        self.did_update = False

    def __eq__(self, other: Any) -> bool:
        """
        Checks if the expected output equals the actual output.
        If the `--accept-tests` flag is set to `1`, the expected output will be updated with the actual output
        :param other: value to compare against, must survive the str() operator
        :return: True if equals or were updated, False if unequal
        """
        are_equal = self.content == str(other)
        if self.do_update and not are_equal:
            self.expected_output_path.write_text(str(other))
            if not self.expected_output_path.is_symlink():
                self.logger.warning(
                    "snapshot didn't propagate, the updated file can be found here: %s",
                    self.expected_output_path.absolute(),
                )
            self.did_update = True
            return True
        return are_equal

    def __repr__(self) -> str:
        return f"{self.content!r}"

    def __str__(self) -> str:
        return self.content


@pytest.fixture
def snapshot(
    do_snapshot_update: bool, logger: Logger, tmp_path: Path
) -> Generator[Callable[[str], Snapshot], Any, None]:
    """
    create a snapshot for the given output file
    the snapshot must be the **left hand operator** of the equal check.
    Example usage: `snapshot("eval_out.exp") == actual_out`

    When either the `--accept-tests` flag or the `_NIX_TEST_ACCEPT` environment variable is set,
    the snapshot will update the expected output with the actual output.

    The given path is relative to the temporary test dir (provided by either `files` or `tmp_path`)
    It is recommended to make the file a symlink back to the file within functional2
    as otherwise the expected output won't be updated.
    :return: a snapshot object which one can use `==` on
    """
    tmp_path = tmp_path / "test-home"
    tmp_path.mkdir(parents=True, exist_ok=True)
    snaps: list[Snapshot] = []

    def create_snapshot(expected_output_path: str) -> Snapshot:
        exp_path = tmp_path / expected_output_path
        if not exp_path.is_symlink():
            logger.warning(
                "expected output file isn't a symlink. When accepting the tests output, the update might not propagate"
            )
        obj = Snapshot(exp_path, do_snapshot_update, logger)
        snaps.append(obj)
        return obj

    yield create_snapshot
    if any(s.did_update for s in snaps):
        pytest.skip("Updated Golden Files")


def pytest_assertrepr_compare(config: Config, op: str, left: Any, right: Any) -> list[str] | None:
    if not isinstance(left, Snapshot) or op != "==" or not isinstance(right, str):
        return None
    left: Snapshot
    right: str

    exp_lines = str(left).splitlines()
    act_lines = right.splitlines()
    expl = [
        f"snapshot({left.expected_output_path.name}) == output. Consider using `--accept-tests` to update the golden files."
    ]
    if config.get_verbosity():
        expl.append("The following lines were mismatched:")
        for i, (exp, act) in enumerate(zip(exp_lines, act_lines)):
            if exp == act:
                continue
            expl.append(f"{i + 1}: - {exp}")
            expl.append(f"{i + 1}: + {act}")

    return expl
