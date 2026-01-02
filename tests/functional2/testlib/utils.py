import builtins
import types
import typing
from pathlib import Path
from textwrap import dedent
from types import UnionType
from typing import Any, Literal, get_args, get_origin

from testlib.environ import environ
from testlib.fixtures.file_helper import (
    CopyFile,
    CopyTree,
    FileDeclaration,
    merge_file_declaration,
    Fileish,
    CopyTemplate,
    File,
)

# Things have to be resolved from top to bottom, because otherwise the tests behave flakey
# due to the internal file structure

testlib_folder = Path(__file__).parent.absolute()

global_assets_folder = testlib_folder / "global_assets"

functional2_base_folder = testlib_folder.parent
"""
Resolves to `tests/functional2` folder
"""


test_base_folder = functional2_base_folder.parent
"""
Resolves the base path to the `tests` folder of the project
This ensures that the relative path all start with functional2.
"""

lix_base_folder = test_base_folder.parent
"""
Resolves to the base path of the lix git repository
"""


def get_functional2_files(additional_files: FileDeclaration | None = None) -> FileDeclaration:
    """
    Returns the very basic Configuration of functional2 (i.e. its init and pyproject toml file)
    Note that this does not include the conftest.py nor anything from testlib
    as those might want to be configured manually
    :param additional_files: any additional files, one wants to have
    :return: file declaration to be used for the files fixtures to set up functional2 within a temp folder
    """
    if additional_files is None:
        additional_files = {}
    return merge_file_declaration(
        {"functional2": {"pyproject.toml": CopyFile(functional2_base_folder / "pyproject.toml")}},
        additional_files,
    )


def get_functional2_files_with_testlib(
    additional_files: FileDeclaration | None = None, no_tests: bool = True
) -> FileDeclaration:
    if additional_files is None:
        additional_files = {}
    if no_tests:

        def ignore(_: Any, names: list[str]) -> list[str]:
            return [name for name in names if name.startswith("test_")]
    else:
        ignore = None
    return get_functional2_files(
        merge_file_declaration(
            {
                "functional2": {
                    "conftest.py": CopyFile(functional2_base_folder / "conftest.py"),
                    "testlib": CopyTree(functional2_base_folder / "testlib", ignore=ignore),
                }
            },
            additional_files,
        )
    )


def get_functional2_lang_files(additional_files: FileDeclaration | None = None) -> FileDeclaration:
    if additional_files is None:
        additional_files = {}
    return get_functional2_files_with_testlib(
        merge_file_declaration(
            {
                "functional2": {
                    "lang": {
                        "__init__.py": CopyFile(functional2_base_folder / "lang/__init__.py"),
                        "lang_util.py": CopyFile(functional2_base_folder / "lang/lang_util.py"),
                        "test_lang.py": CopyFile(functional2_base_folder / "lang/test_lang.py"),
                        "lib.nix": CopyFile(functional2_base_folder / "lang/lib.nix"),
                    }
                }
            },
            additional_files,
        )
    )


def is_value_of_type(value: Any, expected_type: type[Any] | UnionType) -> bool:
    """
    checks if the given value conforms to the type.
    This function is useful, to check if all values conform to the inner iterable type
    :param value: value to check
    :param expected_type: what type each item should be; must be a non-generic type or one of {dict, list, set, UnionType, Literal}
    :return: True if it conforms, otherwise False
    """
    supported_origin_types = {dict, list, set, type, UnionType, Literal}
    origin = get_origin(expected_type)
    if expected_type is Any:
        return True
    match origin:
        case None:
            return isinstance(value, expected_type)
        case types.UnionType:
            return any(is_value_of_type(value, t) for t in get_args(expected_type))
        case typing.Literal:
            return value in get_args(expected_type)
        case builtins.type:
            return value is get_args(expected_type)[0]
        case builtins.list | builtins.set | builtins.dict:
            matches_origin = isinstance(value, origin)
            if not matches_origin:
                return False
            matches = all(is_value_of_type(v, get_args(expected_type)[0]) for v in value)
            if origin is dict:
                # also check the value side of dicts
                matches = matches and all(
                    is_value_of_type(v, get_args(expected_type)[1]) for v in value.values()
                )
            return matches
        case _:
            msg = f"Unsupported expected_type. Must be a non-generic or one of {supported_origin_types!r}"
            raise ValueError(msg)


def get_global_asset(name: str) -> Fileish:
    if name == "config.nix":
        return CopyTemplate(
            functional2_base_folder / "testlib" / "global_assets" / "config.nix.template",
            {
                "system": environ.get("system"),
                # Either just the build shell or entire global path if we are darwin
                "path": environ.get("BUILD_TEST_ENV") or environ.get("PATH"),
                "shell": Path(environ.get("BUILD_TEST_SHELL") or "/bin") / "bash",
            },
        )
    return CopyFile(functional2_base_folder / "testlib" / "global_assets" / name)


def get_global_asset_pack(name: str) -> FileDeclaration:
    def folder_to_assets(folder: str) -> FileDeclaration:
        return {
            f.name: get_global_asset(f"{folder}/{f.name}")
            for f in (global_assets_folder / folder).iterdir()
        }

    match name:
        case ".git":
            return {
                ".git": {
                    "config": File(
                        dedent("""
                    [core]
                        repositoryformatversion = 0
                        filemode = true
                        bare = false
                        logallrefupdates = true

                    [user]
                        email = "tests+functional2@lix.systems"
                        name = "functional2"
                """)
                    ),
                    "description": File(
                        "Unnamed repository; edit this file 'description' to name the repository."
                    ),
                    "HEAD": File("ref: refs/heads/main"),
                    "info": {"exclude": File("")},
                    "objects": {"info": {}, "pack": {}},
                    "refs": {"heads": {}, "tags": {}},
                }
            }
        # default case, if its just config.nix + folder content
        case _ if (global_assets_folder / name).exists():
            return {"config.nix": get_global_asset("config.nix")} | folder_to_assets(name)
        case _:
            msg = f"invalid pack name {name!r}"
            raise ValueError(msg)
