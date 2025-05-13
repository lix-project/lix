import json
from collections.abc import Callable
from logging import Logger
from pathlib import Path

import pytest
import yaml
from _pytest.fixtures import FixtureRequest
from _pytest.python import Metafunc

from functional2.lang.lang_util import LangTest, fetch_all_lang_tests, LangTestRunner
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


def pytest_generate_tests(metafunc: Metafunc):
    """
    This hook parametrizes the parser and eval tests with all test cases found in functional2/lang
    :param metafunc: the test function to parametrize, provided by pytest
    """
    func_name = metafunc.function.__name__
    tests, invalid_tests = fetch_all_lang_tests()
    if func_name == "test_invalid_configuration":
        metafunc.parametrize(("name", "reasons"), [(t.name, t.reasons) for t in invalid_tests])
        return
    selected_runner: LangTestRunner
    match func_name:
        case "test_eval":
            selected_runner = LangTestRunner.EVAL_OKAY
        case "test_xfail_eval":
            selected_runner = LangTestRunner.EVAL_FAIL
        case "test_parser":
            selected_runner = LangTestRunner.PARSE_OKAY
        case "test_xfail_parser":
            selected_runner = LangTestRunner.PARSE_FAIL
        case _:
            return
    selected_tests = tests[selected_runner]
    if len(selected_tests) > 0:
        # ignoring type here, because map doesn't recognize the typing correctly
        # due to returning multiple things and it expecting a single generic
        files, flags, ids = zip(*map(LangTest.to_params, selected_tests))  # type: ignore
    else:
        files, flags, ids = [], [], []
    metafunc.parametrize(("files", "flags"), zip(files, flags), ids=ids, indirect=True)


@pytest.fixture
def flags(request: FixtureRequest) -> list[str]:
    return request.param


def _cleanup_output(stdout: str, stderr: str, origin: Path) -> tuple[str, str]:
    """
    Cleans up any paths present in stdout and stderr by replacing them with placeholders
    """
    test_path = str(origin)
    clean_out = stdout.replace(test_path, "/pwd")
    clean_err = stderr.replace(test_path, "/pwd")
    return clean_out, clean_err


def test_parser(files: Path, nix: Nix, flags: list[str], snapshot: Callable[[str], Snapshot]):
    nix_command = nix.nix_instantiate(
        ["--parse", *flags, files / "in.nix"],
        # TODO(Commentator2.0): Mirrors behavior of init.sh from functional
        #  keep this for migration, but make it declarative afterwards
        #  and only active for the tests which need it
        flake=True,
    )
    result = nix_command.run().ok()
    stdout, stderr = _cleanup_output(result.stdout_s, result.stderr_s, files)

    # Taken from https://stackoverflow.com/a/39681672
    # Pyyaml does not give list items extra indentation, unlike pretty much everyone else.
    # For migration compatibility with the `yq` output from the functional/lang tests, we
    # use a custom dumper that produces the correct indentation.
    # Upstream issue (open since 2018): https://github.com/yaml/pyyaml/issues/234
    class CustomFixedIndentationDumper(yaml.Dumper):
        def increase_indent(self, flow: bool = False, indentless: bool = False):  # noqa: ARG002
            super().increase_indent(flow, False)

    result_obj = json.loads(stdout)
    result_yaml = yaml.dump(
        result_obj, Dumper=CustomFixedIndentationDumper, default_flow_style=False
    )
    # parser out are in a yaml format, which is why we convert it here for better viewing and editing
    assert snapshot("out.exp") == result_yaml
    assert snapshot("err.exp") == stderr


def test_xfail_parser(files: Path, nix: Nix, flags: list[str], snapshot: Callable[[str], Snapshot]):
    nix_command = nix.nix_instantiate(["--parse", *flags, files / "in.nix"], flake=True)
    result = nix_command.run().expect(1)
    stdout, stderr = _cleanup_output(result.stdout_s, result.stderr_s, files)

    assert snapshot("out.exp") == stdout
    assert snapshot("err.exp") == stderr


def test_eval(files: Path, nix: Nix, flags: list[str], snapshot: Callable[[str], Snapshot]):
    nix_command = nix.nix_instantiate(["--eval", "--strict", *flags, files / "in.nix"], flake=True)
    result = nix_command.run().ok()
    stdout, stderr = _cleanup_output(result.stdout_s, result.stderr_s, files)

    assert snapshot("out.exp") == stdout
    assert snapshot("err.exp") == stderr


def test_xfail_eval(files: Path, nix: Nix, flags: list[str], snapshot: Callable[[str], Snapshot]):
    nix_command = nix.nix_instantiate(
        ["--eval", "--strict", "--show-trace", *flags, files / "in.nix"], flake=True
    )
    result = nix_command.run().expect(1)
    stdout, stderr = _cleanup_output(result.stdout_s, result.stderr_s, files)

    assert snapshot("out.exp") == stdout
    assert snapshot("err.exp") == stderr


def test_invalid_configuration(name: str, reasons: list[str], logger: Logger):
    msg = f"Invalid configuration for {name!r}: {reasons}"
    logger.error(msg)
    pytest.fail(msg)
