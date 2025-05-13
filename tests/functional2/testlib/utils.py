from pathlib import Path

from functional2.testlib.fixtures.file_helper import (
    CopyFile,
    CopyTree,
    FileDeclaration,
    merge_file_declaration,
)


# Things have to be resolved from top to bottom, because otherwise the tests behave flakey
# due to the internal file structure

functional2_base_folder = Path(__file__).parent.parent.absolute()
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
        {
            "functional2": {
                "__init__.py": CopyFile(functional2_base_folder / "__init__.py"),
                "pyproject.toml": CopyFile(functional2_base_folder / "pyproject.toml"),
            }
        },
        additional_files,
    )


def get_functional2_files_with_testlib(
    additional_files: FileDeclaration | None = None,
) -> FileDeclaration:
    if additional_files is None:
        additional_files = {}
    return get_functional2_files(
        merge_file_declaration(
            {
                "functional2": {
                    "conftest.py": CopyFile(functional2_base_folder / "conftest.py"),
                    "testlib": CopyTree(functional2_base_folder / "testlib"),
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
