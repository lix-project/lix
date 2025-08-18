import dataclasses
import logging
import re
from enum import StrEnum
from functools import cache
from pathlib import Path
from typing import Any, NamedTuple, ClassVar
from collections.abc import Generator

import tomllib
from functional2.testlib.fixtures.file_helper import AssetSymlink, CopyFile, FileDeclaration
from functional2.testlib.utils import is_value_of_type, test_base_folder
from tomllib import TOMLDecodeError

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

SUFFIX_REGEX = r"-[\w-]+?"
NAME_PATTERN_GENERIC_EXP = re.compile(
    rf"{LangTestRunner.as_regex_selector()}(?P<suffix>{SUFFIX_REGEX})?"
)
NAME_PATTERN_IN_FILE = rf"in({SUFFIX_REGEX})?.nix"


class InFile(NamedTuple):
    name: str
    suffix: str

    @classmethod
    def parse(cls, name_str: str) -> "InFile":
        """
        Parses the given name into a InFile object
        :param name_str: string to parse
        :raises ValueError: When The given string was invalid
        :return: InFile named tuple, on successful parse
        """
        match_ = re.fullmatch(NAME_PATTERN_IN_FILE, name_str)
        if match_ is None:
            msg = f"invalid in-file name {name_str!r}"
            raise ValueError(msg)
        return cls(match_.group(0), match_.group(1) or "")


class LangTest:
    ids: ClassVar[set[str]] = set()
    """
    Set of all existing Ids
    """

    def __init__(
        self,
        name: str,
        folder_name: str,
        runner: LangTestRunner,
        in_file: InFile,
        flags: list[str] | None = None,
        extra_files: list[str] | None = None,
    ):
        """
        Internal class to represent a lang test
        :param name: name of the explicit test (e.g. "eval-depr" or "eval-allow-depr")
        :param folder_name: the folder / group of tests this one originates from (e.g. "nul_bytes")
        :param runner: which runner to run this test on (e.g. EVAL_FAIL or PARSE_OKAY)
        :param in_file: what the input file is
        :param flags: additional flags provided for nix
        :param extra_files: any additional files which should be copied into the tests directory
        :raises ValueError(id, msg): when the id constructed from the parameters is not unique
        """
        self.runner = runner
        self.flags = flags or []
        self.folder = folder_name
        self.extra_files = extra_files or []
        self.in_file_name = in_file.name
        self.suffix = in_file.suffix
        self.test_name = f"{name}{self.suffix}"
        self.id = LANG_TEST_ID_PATTERN.format(folder_name=self.folder, test_name=self.test_name)
        if self.id in LangTest.ids:
            msg = f"id {self.id!r} is not unique. Please set the 'name' attribute manually"
            raise ValueError(self.id, msg)
        LangTest.ids.add(self.id)

    def _get_files(self) -> FileDeclaration:
        """
        Internal function to turn the metadata into a FileDeclaration used for parametrization
        :return: FileDeclaration object containing all files required for the test
        """
        files = {
            "in.nix": CopyFile(f"{self.folder}/{self.in_file_name}"),
            "lib.nix": CopyFile("lib.nix"),
            "out.exp": AssetSymlink(f"{self.folder}/{self.test_name}.out.exp"),
            "err.exp": AssetSymlink(f"{self.folder}/{self.test_name}.err.exp"),
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
        return self._get_files(), self.flags, self.id


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


@dataclasses.dataclass
class LangTestDefinition:
    """
    A parsed object of a singular `test` section from the `test.toml` file
    """

    runner: LangTestRunner
    name: str
    flags: list[str]
    extra_files: list[str]
    matrix: bool
    in_: list[InFile]

    @classmethod
    def parse(cls, dict_: dict[str, Any], in_file_names: list[str]) -> "LangTestDefinition":
        """
        Parses the given dict to a LangTestDefinition object.
        :param dict_: content of a singular test section within the `test.toml` file
        :param in_file_names: a list of all existing in files, used as a default for matrix tests
        :return: LangTestDefinition object containing the parsed information
        :raises ValueError(test_name, [*reasons]): when any information was invalid
        """
        issues = []
        runner_name = dict_.pop("runner", "")
        try:
            runner = LangTestRunner(runner_name)
        except ValueError:
            issues.append(INVALID_TESTER_NAME % runner_name)
            runner = None
        extra_files = dict_.pop("extra-files", [])
        if not is_value_of_type(extra_files, list[str]):
            issues.append(
                f"invalid value type for 'extra_files': {extra_files}, expected a list of strings"
            )

        flags = dict_.pop("flags", [])
        if not is_value_of_type(flags, list[str]):
            issues.append(f"invalid value type for 'flags': {flags}, expected a list of strings")
        test_name = dict_.pop("name", runner_name)

        is_matrix = dict_.pop("matrix", False)
        if not is_value_of_type(is_matrix, bool):
            issues.append(f"invalid type for 'matrix': {is_matrix}, expected a boolean")

        in_file_def = dict_.pop("in", None)
        if is_matrix:
            in_correct_type = in_file_def is None or is_value_of_type(in_file_def, list[str])
            in_file_names = in_file_def or in_file_names
        else:
            in_correct_type = is_value_of_type(in_file_def, str | None)
            in_file_names = [in_file_def] if in_file_def is not None else ["in.nix"]

        in_files: list[InFile] = []
        if not in_correct_type or len(in_file_names) == 0:
            issues.append(
                f"invalid type for 'in': {in_file_def!r}, expected a{' list of' if is_matrix else ''} string"
            )
        else:
            # Only check naming if the type is actually correct
            for i, name in enumerate(in_file_names):
                try:
                    in_files.append(InFile.parse(name))
                except ValueError as e:
                    issues.append(f"{e.args[0]} at position {i} for 'in'")

        if dict_:
            issues.append(f"unexpected arguments: {list(dict_.keys())!r}")

        if issues:
            raise ValueError(test_name, issues)
        return cls(
            runner=runner,
            name=test_name,
            flags=flags,
            extra_files=extra_files,
            matrix=is_matrix,
            in_=in_files,
        )

    def build(self, folder: str) -> tuple[list[LangTest], list[InvalidLangTest]]:
        """
        Builds this LangTestDefinition into LangTests.
        :param folder: test group folder name this Definition was created from
        :return: list of valid LangTests and a list of InvalidLangTests (created when the id wasn't unique)
        """
        tests = []
        invalid = []
        for file in self.in_:
            try:
                tests.append(
                    LangTest(self.name, folder, self.runner, file, self.flags, self.extra_files)
                )
            except ValueError as e:
                id_, *reasons = e.args
                invalid.append(InvalidLangTest(id_, reasons))
        return tests, invalid


def parse_toml(
    toml_content: dict[str, Any], in_files: list[str]
) -> Generator[LangTestDefinition | ValueError, None, None]:
    """
    Parses the given toml content to LangTestDefinitions.
    :param toml_content: content of the `test.toml` file
    :param in_files: a list of all in files, used as a default for matrix tests
    :returns: Generator yielding `LangTestDefinition`s for successful parses and ValueError(test_name, [*reasons]) for parse failures
    """
    test_attr_name = "test"
    issues = []
    test_dicts = toml_content.pop(test_attr_name, [])
    if not test_dicts:
        issues.append(f"key {test_attr_name!r} not found or empty")
        test_dicts = []
    elif not is_value_of_type(test_dicts, list[dict[str, Any]]):
        issues.append(f"Invalid type for {test_attr_name!r}. Expected an Array of Tables")
        test_dicts = []

    if len(toml_content) > 0:
        issues.append(
            f"unexpected key(s) {list(toml_content.keys())}; expected only {test_attr_name!r}"
        )

    for declaration in test_dicts:
        try:
            yield LangTestDefinition.parse(declaration, in_files)
        except ValueError as e:
            yield e

    if issues:
        yield ValueError("", issues)


def _collect_toml_test_group(folder: Path) -> tuple[list[LangTest], list[InvalidLangTest]]:
    """
    Collects all tests, declared by a `test.toml` file within the given folder
    :param folder: base folder to collect tests in
    :return: a list of valid test configurations and a list of invalid test configurations
    """
    parent_name = folder.name
    invalid_tests: list[InvalidLangTest] = []
    tests: list[LangTest] = []
    # files starting with "_" will be ignored, e.g. "__pycache__"
    all_files = {f.name for f in folder.iterdir() if not f.name.startswith("_")}
    in_files = [f for f in all_files if re.fullmatch(rf"in({SUFFIX_REGEX})?\.nix", f) is not None]
    # used to make sure all files are actually used
    unused_files = all_files - {"test.toml"}

    try:
        infos: dict[str, Any] = tomllib.loads((folder / "test.toml").read_text())
    except TOMLDecodeError as e:
        return [], [InvalidLangTest(parent_name, [f"couldn't parse toml: {e!r}"])]

    for test in parse_toml(infos, in_files):
        test_name = test.name if isinstance(test, LangTestDefinition) else test.args[0]
        full_name = LANG_TEST_ID_PATTERN.format(folder_name=parent_name, test_name=test_name)

        if isinstance(test, ValueError):
            invalid_tests.append(InvalidLangTest(full_name, test.args[1]))
            continue

        new_tests, new_invalids = test.build(parent_name)
        tests += new_tests
        invalid_tests += new_invalids

        unused_files -= set(test.extra_files)
        for t in new_tests:
            unused_files -= {t.in_file_name, f"{t.test_name}.out.exp", f"{t.test_name}.err.exp"}

    # Only throw issues about unused files when the group is otherwise valid
    # this is done to avoid having unused files showing up due to invalid configurations,
    # which should be fixed first and the unused files would just clutter the error messages in that case
    if len(unused_files) > 0 and not invalid_tests:
        invalid_tests.append(
            InvalidLangTest(
                parent_name, [f"the following files weren't referenced: {unused_files!r}"]
            )
        )
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
    all_files = set(folder.iterdir())
    unused = {f.name for f in all_files if not f.name.startswith("_")}
    for file in all_files:
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
            match = re.fullmatch(NAME_PATTERN_GENERIC_EXP, test_name)
            if match is None:
                if re.match(LangTestRunner.as_regex_selector(), test_name) is None:
                    reason = INVALID_TESTER_NAME % test_name
                else:
                    reason = f"incorrectly formatted test name: {test_name!r}"
                invalid_tests.append(InvalidLangTest(full_name, [reason]))
                continue
            runner_name, suffix = match.groups()
            runner = LangTestRunner(runner_name)
            suffix = suffix or ""

            tests.append(
                LangTest(runner_name, folder.name, runner, InFile(f"in{suffix}.nix", suffix))
            )
            unused -= {file.name, f"in{suffix or ''}.nix"}

    # Only throw issues about unused files when the group is otherwise valid
    # this is done to avoid having unused files showing up due to invalid configurations,
    # which should be fixed first and the unused files would just clutter the error messages in that case
    if len(unused) > 0 and not invalid_tests:
        invalid_tests.append(
            InvalidLangTest(parent_name, [f"the following files weren't referenced: {unused!r}"])
        )
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
        if node.is_file() or node.name == "assets" or node.name.startswith("_"):
            continue

        # ignore test groups, which have a py file, as those are set up fully custom
        # and expected to be collected by pytest and not by us
        if list(node.glob("*.py")):
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
