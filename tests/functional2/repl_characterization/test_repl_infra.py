from textwrap import dedent
from pathlib import Path
from testlib.utils import functional2_base_folder
from testlib.fixtures.file_helper import CopyFile
from testlib.fixtures.file_helper import merge_file_declaration
from testlib.utils import get_functional2_files_with_testlib
from testlib.fixtures.file_helper import FileDeclaration
from testlib.fixtures.file_helper import File
from testlib.fixtures.file_helper import with_files
import pytest
from testlib.fixtures.command import Command

pytestmark = pytest.mark.no_daemon


def get_functional2_repl_files(files: FileDeclaration | None = None) -> FileDeclaration:
    repl_base = functional2_base_folder / "repl_characterization"
    files = {"functional2": {"repl_characterization": files or {}}}
    total_files = merge_file_declaration(
        files,
        {
            "functional2": {
                "repl_characterization": {
                    "__init__.py": File(""),
                    "test_repl.py": CopyFile(repl_base / "test_repl.py"),
                }
            }
        },
    )
    return get_functional2_files_with_testlib(total_files)


@pytest.mark.parametrize("pytest_command", [["-k", "repl_char"]], indirect=True)
@with_files(
    get_functional2_repl_files(
        {
            "repl_basics": {
                "nya.md": File(
                    dedent("""
                        ```nix
                        1+1
                        ```
                        ```output
                        2

                        ```
                    """)
                )
            }
        }
    )
)
def test_trivial_succeeds(pytest_command: Command):
    res = pytest_command.run().ok()
    assert "repl_basics:nya.md-None] PASSED" in res.stdout_s


@pytest.mark.parametrize("pytest_command", [(["-k", "repl_char"], False)], indirect=True)
@with_files(
    get_functional2_repl_files(
        {
            "repl_basics": {
                "nya.md": File(
                    dedent("""
                        ```nix
                        1+1
                        ```
                        ```output
                        3

                        ```
                    """)
                )
            }
        }
    )
)
def test_trivial_fails(pytest_command: Command):
    res = pytest_command.run().expect(1)
    assert (
        "FAILED repl_characterization/test_repl.py::test_repl_char[repl_basics:nya.md-None]"
        in res.stdout_s
    )


@pytest.mark.parametrize("pytest_command", [["-k", "repl_char", "--accept-tests"]], indirect=True)
@with_files(
    get_functional2_repl_files(
        {
            "repl_basics": {
                "nya.md": File(
                    dedent("""
                        ```nix
                        1+1
                        ```
                        ```output
                        3

                        ```
                    """)
                )
            }
        }
    )
)
def test_updates(pytest_command: Command, files: Path):
    md_file = files / "functional2" / "repl_characterization" / "repl_basics" / "nya.md"
    assert "output\n3" in md_file.read_text()
    pytest_command.run().ok()
    assert "output\n3" not in md_file.read_text()
    assert "output\n2" in md_file.read_text()


@pytest.mark.parametrize("pytest_command", [["-k", "repl_char", "--accept-tests"]], indirect=True)
@with_files(
    get_functional2_repl_files(
        {
            "repl_basics": {
                "nya.md": File(
                    dedent("""
                        ```nix
                        1+1
                        ```
                    """)
                )
            }
        }
    )
)
def test_trivial_creates_block(pytest_command: Command, files: Path):
    f = files / "functional2"
    assert f.exists()
    f /= "repl_characterization"
    assert f.exists()
    f /= "repl_basics"
    assert f.exists()
    md_file = files / "functional2" / "repl_characterization" / "repl_basics" / "nya.md"
    assert "output" not in md_file.read_text()
    pytest_command.run().ok()
    assert "```output\n2\n\n```" in md_file.read_text()
