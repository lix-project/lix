import shutil
from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any
from collections.abc import Callable, Iterable

import pytest

from functional2.testlib.fixtures.env import ManagedEnv
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
    def __init__(
        self, tree_base: str | Path, ignore: Callable[[str, list[str]], Iterable[str]] | None = None
    ):
        """
        Declares a folder as a copy of an existing folder
        :param tree_base: base folder of the tree being copied
        """
        self.tree_base = tree_base
        self.ignore = ignore

    def copy_to(self, path: Path, origin: Path):
        orig = self.tree_base if isinstance(self.tree_base, Path) else origin / self.tree_base
        shutil.copytree(orig, path, dirs_exist_ok=True, ignore=self.ignore)


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


class Symlink(Fileish):
    def __init__(self, target: str):
        """
        Declares a file as a symlink to a different location.

        NOTE: due to limitations of symlinks on Windows, tests using this might be flaky and fail!!
        :param target: Path to the target where the symlink should be pointing
        """
        self.target = target

    def copy_to(self, path: Path, _origin: Path):
        path.symlink_to(self.target)


class AssetSymlink(Fileish):
    def __init__(self, source: str):
        """
        Declares a file as a symlink to a local asset file.

        NOTE: due to limitations of symlinks on Windows, tests using this might be flakey and fail!!
        :param source: Path to the source where the symlink should be pointing.
            Paths are relative to the current test's module.
        :raise ValueError: When the given source path is absolute
        """
        self.source = source

    def copy_to(self, path: Path, origin: Path):
        if Path(self.source).is_absolute():
            msg = "absolute paths are not allowed"
            raise ValueError(msg)
        target_path = origin / self.source
        path.symlink_to(target_path)


type FileDeclaration = dict[str, Fileish | "FileDeclaration"]


def merge_file_declaration(a: FileDeclaration, b: FileDeclaration) -> FileDeclaration:
    result = {}
    for key in a.keys() | b.keys():
        if (key in a) ^ (key in b):
            result[key] = a.get(key) or b.get(key)
            continue
        if isinstance(a[key], Fileish) or isinstance(b[key], Fileish):
            msg = "Cannot merge files; got two different values for the same path"
            raise ValueError(msg, key)
        try:
            result[key] = merge_file_declaration(a[key], b[key])
        except ValueError as e:
            msg, path = e.args
            raise ValueError(msg, f"{key}/{path}")

    return result


def _init_files(files: FileDeclaration, current_folder: Path, caller_path: Path) -> None:
    """
    This internal function is needed because one cannot call a fixture directly since pytest 4.0
    """

    for name, definition in files.items():
        destination = current_folder / name
        if isinstance(definition, Fileish):
            definition.copy_to(destination, caller_path)
        else:
            # Initialize subdirectory
            destination.mkdir()
            _init_files(definition, destination, caller_path)


def with_files(*files: FileDeclaration) -> Callable[[Any], Callable[[Any], None]]:
    def decorator(func: Callable[[Any], None]) -> Callable[[Any], None]:
        return pytest.mark.usefixtures("files")(
            pytest.mark.parametrize("files", files, indirect=True)(func)
        )

    return decorator


@pytest.fixture
def files(env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    """
    Initializes the given files into the TempDir of the test.
    This ensures all necessary files and only those are present
    To use this add `@with_files(list_of_your_files_to_test, more_files_to_test)` above your test.
    The test is run once for each of the sets of files provided as the second argument.
    Each Set of files should be of the :py:type:`FileDeclaration` type

    :param env: environment to get the HOME path from
    :param request: Fixture information provided by pytest, used to parametrize the files
    :return: Path to where the files were created
    """
    home = env.dirs.home
    if hasattr(request, "param"):
        _init_files(request.param, home, request.path.parent)
    return home
