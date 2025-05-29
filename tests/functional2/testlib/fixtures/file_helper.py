import shutil
from abc import ABC, abstractmethod
from enum import StrEnum
from pathlib import Path
from typing import Any

import pytest

from functional2.testlib.fixtures.formatter import BalancedTemplater


class Fileish(ABC):
    """
    Baseclass, which allows files to be copied declaratively
    """

    @abstractmethod
    def copy_to(self, path: Path, origin: Path) -> None:
        """
        Copies this file to the given TempDir
        :param path: TempDir for the test
        :param origin: Directory the tests originates in. Used to adjust relative paths
        """


class _ByContentFileish(Fileish, ABC):
    def __init__(self, mode: int | None = None):
        self.mode = mode

    @abstractmethod
    def get_content(self, origin: Path) -> str:
        """
        Returns the content, which should be present in the current path
        :return: content as a string
        """

    def copy_to(self, path: Path, origin: Path) -> None:
        path.write_text(self.get_content(origin))
        if self.mode is not None:
            path.chmod(self.mode)


class File(_ByContentFileish):
    def __init__(self, file_contents: str, mode: int | None = None):
        """
        Declares a file by its content
        :param file_contents: content of the file as a string
        :param mode: Optionally change the mode of the file (e.g. to executable)
        """
        super().__init__(mode)
        self.file_contents = file_contents

    def get_content(self, _: Path) -> str:
        return self.file_contents


class CopyFile(Fileish):
    def __init__(self, source: str | Path):
        """
        Declares a file as a copy of an existing file
        :param source: Path to the file to be copied
        """
        self.source = source

    def copy_to(self, path: Path, origin: Path):
        orig = self.source if isinstance(self.source, Path) else origin / self.source
        shutil.copyfile(orig, path)


class CopyTree(Fileish):
    def __init__(self, tree_base: str | Path):
        """
        Declares a folder as a copy of an existing folder
        :param tree_base: base folder of the tree being copied
        """
        self.tree_base = tree_base

    def copy_to(self, path: Path, origin: Path):
        orig = self.tree_base if isinstance(self.tree_base, Path) else origin / self.tree_base
        shutil.copytree(orig, path, dirs_exist_ok=True)


class CopyTemplate(_ByContentFileish):
    def __init__(self, template: str | Path, values: dict[str, Any], mode: int | None = None):
        """
        Declares a file as an initiated version of the given file template
        :param template: source template's file name. Parameters formatted as `{key_name}` are replaced by corresponding values
        :param values: dictionary of key_name and value to be replaced in the template.
        :param mode: Optionally change the mode of the file (e.g. to executable)
        """
        self.template = template
        self.values = values
        self.content: str | None = None

        super().__init__(mode)

    def get_content(self, origin: Path) -> str:
        template_path = self.template if isinstance(self.template, Path) else origin / self.template
        template_content = template_path.read_text()

        self.content = BalancedTemplater(template_content).substitute(**self.values)

        return self.content


class RelativeTo(StrEnum):
    TEST = "test"
    """
    Base for the given path is the directory of the test
    """
    TARGET = "target"
    """
    Base for the given path is the directory of the target / where all files are created
    """
    ROOT = "root"
    """
    Base for the given path is the root folder (/)
    """
    SELF = "self"
    """
    No base path is given, used to create relative symlinks (e.g. `"../test"`)
    """


class Symlink(Fileish):
    def __init__(self, source: str, relative_to: RelativeTo = RelativeTo.TARGET):
        """
        Declares a file as a symlink to a different location

        NOTE: due to limitations of symlinks on Windows, tests using this might be flakey and fail!!
        :param source: Path to the source where the symlink should be pointing
        :raise ValueError: When the given source is an absolute Path, but relative to wasn't set to ROOT
        """
        self.source = source
        self.relative_to = relative_to

    def copy_to(self, path: Path, origin: Path):
        if self.relative_to is not RelativeTo.ROOT and Path(self.source).is_absolute():
            msg = "absolute paths are only supported when using RelativeTo.ROOT"
            raise ValueError(msg)
        match self.relative_to:
            case RelativeTo.TEST:
                base = origin
            case RelativeTo.TARGET:
                base = path.parent
            case RelativeTo.ROOT:
                base = Path("/")
            case RelativeTo.SELF:
                base = None
            case _:
                msg = f"unknown relativity {self.relative_to}"
                raise ValueError(msg)
        target_path = base / self.source if base is not None else Path(self.source)
        path.symlink_to(target_path)


type FileDeclaration = dict[str, Fileish | "FileDeclaration"]


def merge_file_declaration(a: FileDeclaration, b: FileDeclaration) -> FileDeclaration:
    result = {}
    for key in a.keys() | b.keys():
        if (key in a) ^ (key in b):
            result[key] = a.get(key) or b.get(key)
            continue
        if isinstance(a[key], Fileish) or isinstance(b[key], Fileish):
            msg = "Cannot merge files; got two different values for the same path %s"
            raise ValueError(msg, key)
        result[key] = merge_file_declaration(a[key], b[key])

    return result


def _init_files(files: FileDeclaration, tmp_path: Path, request: pytest.FixtureRequest) -> None:
    """
    This internal function is needed because one cannot call a fixture directly since pytest 4.0
    """

    for name, definition in files.items():
        destination = tmp_path / name
        if isinstance(definition, Fileish):
            definition.copy_to(destination, request.path.parent)
        else:
            # Initialize subdirectory
            destination.mkdir()
            _init_files(definition, destination, request)


@pytest.fixture
def files(tmp_path: Path, request: pytest.FixtureRequest) -> Path:
    """
    Initializes the given files into the TempDir of the test.
    This ensures all necessary files and only those are present
    To use this add `@pytest.mark.parametrize("files", [list_of_your_files_to_test, more_files_to_test], indirect=True)` above your test.
    The test is run once for each of the sets of files provided as the second argument.
    Each Set of files should be of the :py:type:`FileDeclaration` type

    :param tmp_path: TempDir of the test
    :param request: Fixture information provided by pytest, used to parametrize the files
    :return: Path to where the files were created
    """
    _init_files(request.param, tmp_path, request)
    return tmp_path
