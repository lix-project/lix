import logging
import re
from enum import StrEnum
from functools import cache
from pathlib import Path
from typing import Any

import toml
from toml import TomlDecodeError

from functional2.testlib.fixtures.file_helper import FileDeclaration, CopyFile, AssetSymlink
from functional2.testlib.utils import test_base_folder, is_value_of_type

LANG_TEST_ID_PATTERN = "{folder_name}:{test_name}"


class LangTestRunner(StrEnum):
    """
    A list of possible runners for the lang tests on
    The string value is the one present in the filenames / test.toml file as runner
    """

    EVAL_OKAY = "eval-okay"
    EVAL_FAIL = "eval-fail"
    PARSE_OKAY = "parse-okay"
    PARSE_FAIL = "parse-fail"

    @classmethod
    def as_regex_selector(cls) -> str:
        """
        Returns a regex to match any available runner name into a named capturing group called "runner_name"
        :return: a string containing a matching regex
        """
        # For some reason ruff doesn't detect that .value is the string already and not a function
        # it seems to work when accessing `LangTestRunner` but not when using `cls`
        return rf"(?P<runner_name>{'|'.join([runner.value for runner in cls])})"  # type: ignore


INVALID_TESTER_NAME = (
    f"invalid runner name: '%s', must be one of {[runner.value for runner in LangTestRunner]!r}"
)
"""
Base message for invalid runner, to use across collection
"""

SUFFIX_REGEX = re.compile("-[\\w-]+?").pattern
NAMING_PATTERN_LANG_TEST = re.compile(
    rf"{LangTestRunner.as_regex_selector()}(?P<suffix>{SUFFIX_REGEX})?"
)


class LangTest:
    def __init__(
        self,
        test_name: str,
        folder_name: str,
        runner: LangTestRunner,
        flags: list[str] | None = None,
        extra_files: list[str] | None = None,
        suffix: str = "",
    ):
        """
        Internal class to represent a lang test
        :param test_name: name of the explicit test (e.g. "eval-depr" or "eval-allow-depr")
        :param folder_name: the folder / group of tests this one originates from (e.g. "nul_bytes")
        :param runner: which runner to run this test on (e.g. EVAL_FAIL or PARSE_OKAY)
        :param flags: additional flags provided for nix
        :param extra_files: any additional files which should be copied into the tests directory
        :param suffix: suffix of the in file
        """
        self.test_name = test_name
        self.full_name = LANG_TEST_ID_PATTERN.format(
            folder_name=folder_name, test_name=f"{test_name}{suffix}"
        )
        self.runner = runner
        self.flags = flags or []
        self.folder = folder_name
        self.extra_files = extra_files or []
        self.suffix = suffix

    def _get_files(self) -> FileDeclaration:
        """
        Internal function to turn the metadata into a FileDeclaration used for parametrization
        :return: FileDeclaration object containing all files required for the test
        """
        files = {
            "in.nix": CopyFile(f"{self.folder}/in{self.suffix}.nix"),
            "lib.nix": CopyFile("lib.nix"),
            "out.exp": AssetSymlink(f"{self.folder}/{self.test_name}{self.suffix}.out.exp"),
            "err.exp": AssetSymlink(f"{self.folder}/{self.test_name}{self.suffix}.err.exp"),
        }
        for file in self.extra_files:
            # Make sure to add the extra-files requested by the test.toml
            files[file] = CopyFile(f"{self.folder}/{file}")
        return files

    def to_params(self) -> tuple[FileDeclaration, list[str], str]:
        """
        Converts the LangTest to the parameters required for parametrization of the test runners
        :return: a Tuple of the FileDeclaration (used by the `files` fixture), list of flags and a unique id
        """
        return self._get_files(), self.flags, self.full_name


class InvalidLangTest:
    def __init__(self, name: str, reasons: list[str]):
        """
        Metadata class for invalid test configuration, used to later fail a test with the given reasons
        :param name: name of the test which is configured badly
        :param reasons: a list of reasons as to why the configuration is invalid
        """
        self.name = name
        self.reasons = reasons


def _group_lang_tests(tests: list[LangTest]) -> dict[LangTestRunner, list[LangTest]]:
    """
    groups the given list of tests by their runner
    :param tests: list of tests to group
    :return: grouped tests in the following order: EVAL_OKAY, EVAL_FAIL, PARSE_OKAY, PARSE_FAIL
    """
    grouped_tests = {runner_name: [] for runner_name in LangTestRunner}
    for test in tests:
        grouped_tests[test.runner].append(test)
    return grouped_tests


def _collect_toml_test_group(folder: Path) -> tuple[list[LangTest], list[InvalidLangTest]]:
    """
    Collects all tests, declared by a `test.toml` file within the given folder
    :param folder: base folder to collect tests in
    :return: a list of valid test configurations and a list of invalid test configurations
    """
    parent_name = folder.name
    test_declaration = folder / "test.toml"
    try:
        infos: dict[str, Any] = toml.load(test_declaration)
    except TomlDecodeError as e:
        return [], [InvalidLangTest(parent_name, [f"couldn't parse toml: {e!r}"])]
    invalid_tests: list[InvalidLangTest] = []
    tests: list[LangTest] = []

    # suffixes of the in files, e.g.
    # in.nix => ''
    # in-1.nix => '-1'
    # in-some-test.nix => '-some-test'
    # etc
    in_suffixes = [
        suffix.group(1) or ""
        for suffix in [
            re.fullmatch(rf"in({SUFFIX_REGEX})?\.nix", file.name) for file in folder.iterdir()
        ]
        if suffix is not None
    ]

    for test_name, definition in infos.items():
        test_errors: list[str] = []
        full_name = LANG_TEST_ID_PATTERN.format(folder_name=parent_name, test_name=test_name)
        if not isinstance(definition, dict):
            invalid_tests.append(
                InvalidLangTest(
                    full_name, [f"invalid value for {test_name!r}; only tests are expected"]
                )
            )
            continue

        flags = definition.pop("flags", [])
        if not is_value_of_type(flags, list[str]):
            test_errors.append(
                f"invalid value type for 'flags': {flags}, expected a list of strings"
            )

        runner_name = definition.pop("runner", None)
        try:
            runner = LangTestRunner(runner_name)
        except ValueError:
            test_errors.append(INVALID_TESTER_NAME % runner_name)
            runner = None

        extra_files = definition.pop("extra-files", [])
        if not is_value_of_type(extra_files, list[str]):
            test_errors.append(
                f"invalid value type for 'extra_files': {extra_files}, expected a list of strings"
            )

        if len(definition) > 0:
            test_errors.append(f"unexpected arguments: {list(definition.keys())!r}")
        if test_errors:
            invalid_tests.append(InvalidLangTest(full_name, test_errors))
            continue

        # Add a test for each in file
        tests += [
            LangTest(test_name, folder.name, runner, flags, extra_files, in_suffix)
            for in_suffix in in_suffixes
        ]

    return tests, invalid_tests


def _collect_generic_test_group(folder: Path) -> tuple[list[LangTest], list[InvalidLangTest]]:
    """
    Collects all tests within the given folder, if no `test.toml` file is presented for a more explicit test configuration
    :param folder: base folder to collect tests in
    :return: a list of valid test configurations and a list of invalid test configurations
    """
    parent_name = folder.name
    tests: list[LangTest] = []
    invalid_tests: list[InvalidLangTest] = []
    for file in folder.iterdir():
        file: Path
        if file.suffix == ".exp":
            # we cannot use `file.stem` here, as it only removes the last suffix. i.e.
            # `"eval-okay.out.exp".stem` => "eval-okay.out"
            # `"eval-okay.out.exp".split(".")[0]` => "eval-okay"
            # or
            # `"parse-fail-some-name.err.exp".stem` => "parse-fail-some-name.err"
            # `"parse-fail-some-name.err.exp".split(".")[0]` => "parse-fail-some-name"
            test_name = file.name.rsplit(".", 2)[0]
            full_name = LANG_TEST_ID_PATTERN.format(folder_name=parent_name, test_name=test_name)
            match = re.fullmatch(NAMING_PATTERN_LANG_TEST, test_name)
            if match is None:
                if re.match(LangTestRunner.as_regex_selector(), test_name) is None:
                    reason = INVALID_TESTER_NAME % test_name
                else:
                    reason = f"incorrectly formatted test name: {test_name!r}"
                invalid_tests.append(InvalidLangTest(full_name, [reason]))
                continue
            runner_name, suffix = match.groups()
            runner = LangTestRunner(runner_name)
            tests.append(LangTest(runner_name, folder.name, runner, suffix=suffix or ""))
    return tests, invalid_tests


def _collect_test_group(folder: Path) -> tuple[list[LangTest], list[InvalidLangTest]]:
    """
    Collects all tests within the given test group
    :param folder: where the test group are located
    :return: a list of valid test configurations and a list of invalid test configurations
    """
    test_declaration = folder / "test.toml"
    if test_declaration.exists():
        return _collect_toml_test_group(folder)
    return _collect_generic_test_group(folder)


def _collect_all_tests() -> tuple[list[LangTest], list[InvalidLangTest]]:
    """
    Collects all LangTests present in the functional2/lang folder
    :return: a list of valid test configurations and a list of invalid test configurations
    """
    logger = logging.getLogger("lang-test-collector")
    lang_folder = test_base_folder / "functional2/lang"
    tests: list[LangTest] = []
    invalid_tests: list[InvalidLangTest] = []
    for node in lang_folder.iterdir():
        node: Path
        # skip files, as test groups are folders and custom tests will be collected by pytest
        # these are files like this `lang_util.py` or the `lib.nix` etc
        if node.is_file() or node.name == "assets":
            continue

        # ignore test groups, which have a py file, as those are set up fully custom
        # and expected to be collected by pytest and not by us
        if len(list(node.glob("*.py"))):
            logger.info("skipping %s as it contains a py file, assuming custom tests", node)
            continue
        t, i = _collect_test_group(node)
        tests += t
        invalid_tests += i
    return tests, invalid_tests


@cache
def fetch_all_lang_tests() -> tuple[dict[LangTestRunner, list[LangTest]], list[InvalidLangTest]]:
    """
    Collects all LangTests declared within `functional2/lang` and groups them by runner / type
    :return: valid tests as a mapping of runner -> tests for said runner, InvalidLangTests
    """
    tests, invalid_tests = _collect_all_tests()
    return _group_lang_tests(tests), invalid_tests
