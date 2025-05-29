import logging
from collections.abc import Callable
from pathlib import Path
from textwrap import dedent

import pytest
from _pytest.logging import LogCaptureFixture
from functional2.testlib.commands import Command
from functional2.testlib.fixtures.file_helper import (
    CopyFile,
    File,
    FileDeclaration,
    merge_file_declaration,
)
from functional2.testlib.fixtures.snapshot import Snapshot
from functional2.testlib.utils import get_functional2_files


def _get_f2_snapshot_files(additional_files: FileDeclaration) -> FileDeclaration:
    return get_functional2_files(
        merge_file_declaration(
            {
                "functional2": {
                    "testlib": {
                        "__init__.py": CopyFile("../__init__.py"),
                        "fixtures": {
                            "__init__.py": CopyFile("__init__.py"),
                            "snapshot.py": CopyFile("snapshot.py"),
                            "logger.py": CopyFile("logger.py"),
                        },
                    },
                    "conftest.py": File(
                        "pytest_plugins = ('functional2.testlib.fixtures.logger', 'functional2.testlib.fixtures.snapshot')"
                    ),
                }
            },
            additional_files,
        )
    )


def test_snapshot_empty_on_no_file(snapshot: Callable[[str], Snapshot]):
    # no qa here, as we want to exactly check for empty string
    assert snapshot("this_file_does_not_exist") == ""  # noqa: PLC1901


@pytest.mark.parametrize("files", [{"empty_file.txt": File("")}], indirect=True)
@pytest.mark.usefixtures("files")
def test_snapshot_empty_on_empty_file(snapshot: Callable[[str], Snapshot]):
    # no qa here, as we want to exactly check for empty string
    assert snapshot("empty_file.txt") == ""  # noqa: PLC1901


@pytest.mark.parametrize("files", [{"out.exp": File("Hell o' World")}], indirect=True)
@pytest.mark.usefixtures("files")
def test_snapshot_warns_on_non_symlink_file(
    snapshot: Callable[[str], Snapshot], caplog: LogCaptureFixture
):
    with caplog.at_level(logging.WARNING):
        snapshot("out.exp")
    assert len(caplog.records) == 1
    assert (
        caplog.records[0].msg
        == "expected output file isn't a symlink. When accepting the tests output, the update might not propagate"
    )


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [
        (
            _get_f2_snapshot_files(
                {
                    "functional2": {
                        "test_snapshot": {
                            "test_snapshot.py": File(
                                dedent("""
                                    def test_noupdate(do_snapshot_update):
                                        assert not do_snapshot_update
                                    """)
                            )
                        }
                    }
                }
            ),
            ([], False),
        )
    ],
    indirect=True,
)
@pytest.mark.usefixtures("files")
def test_do_update_false_when_none_set(pytest_command: Command):
    pytest_command.run().ok()


_update_test_files = _get_f2_snapshot_files(
    {
        "functional2": {
            "test_snapshot": {
                "test_snapshot.py": File(
                    dedent("""
                        def test_does_update(do_snapshot_update):
                            assert do_snapshot_update
                        """)
                )
            }
        }
    }
)


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [(_update_test_files, ([], False)), (_update_test_files, (["--accept-tests"], False))],
    indirect=True,
)
@pytest.mark.parametrize("set_env", [False, True])
@pytest.mark.usefixtures("files")
def test_do_update_true_when_any_set(pytest_command: Command, set_env: bool):
    if not (set_env or "--accept-tests" in pytest_command.argv):
        pytest.skip("not this test case")
    if set_env:
        pytest_command.update_env(_NIX_TEST_ACCEPT="1")
    pytest_command.run().ok()


def _snapshot_test_files(content: str) -> FileDeclaration:
    return _get_f2_snapshot_files(
        {
            "functional2": {
                "test_snapshot": {
                    "test_snapshot.py": File(
                        dedent(f"""
                            def test_snapshot(snapshot, tmp_path):
                                (tmp_path / "out.exp").write_text("{content}")
                                assert snapshot("out.exp") == "plush plush"
                            """)
                    )
                }
            }
        }
    )


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [(_snapshot_test_files("plush plush"), (["--accept-tests"], False))],
    indirect=True,
)
@pytest.mark.usefixtures("files")
def test_snapshot_no_updated_when_equal(pytest_command: Command):
    res = pytest_command.run().ok()
    assert "the updated file can be found here" not in res.stdout_plain


@pytest.mark.parametrize(
    ("files", "pytest_command"), [(_snapshot_test_files("fops plush"), ([], False))], indirect=True
)
@pytest.mark.usefixtures("files")
def test_snapshot_fails_on_diff(pytest_command: Command):
    res = pytest_command.run().expect(1)
    assert "FAILED test_snapshot/test_snapshot.py::test_snapshot" in res.stdout_plain


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [(_snapshot_test_files("fops plush"), (["--accept-tests"], False))],
    indirect=True,
)
def test_snapshot_updates_diff(files: Path, pytest_command: Command):
    output_file = files / "pytest_files/test_snapshot0/out.exp"
    pytest_command.run().ok()
    assert output_file.read_text() == "plush plush"


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [(_snapshot_test_files("fops plush"), (["--accept-tests"], False))],
    indirect=True,
)
def test_snapshot_updates_shares_updated_location_without_symlink(
    files: Path, pytest_command: Command
):
    expected_path = "pytest_files/test_snapshot0/out.exp"
    output_file = files / expected_path
    res = pytest_command.run().ok()
    assert output_file.read_text() == "plush plush"
    assert "the updated file can be found here" in res.stdout_plain
    assert expected_path in res.stdout_plain


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [(_snapshot_test_files("fops plush"), (["--accept-tests"], False))],
    indirect=True,
)
def test_snapshot_marks_skip_after_update(files: Path, pytest_command: Command):
    expected_path = "pytest_files/test_snapshot0/out.exp"
    output_file = files / expected_path
    res = pytest_command.run().ok()
    assert output_file.read_text() == "plush plush"
    assert "test_snapshot/test_snapshot.py::test_snapshot SKIPPED (Updated" in res.stdout_plain


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [
        (
            _get_f2_snapshot_files(
                {
                    "functional2": {
                        "test_snapshot": {
                            "test_snapshot.py": File(
                                dedent("""
                                    def test_snapshot(snapshot, tmp_path):
                                        (tmp_path / "out.exp").write_text("fops plush")
                                        (tmp_path / "err.exp").write_text("shork plush")
                                        assert snapshot("out.exp") == "plush plush"
                                        assert snapshot("err.exp") == "blobhaj"
                                    """)
                            )
                        }
                    }
                }
            ),
            (["--accept-tests"], False),
        )
    ],
    indirect=True,
)
def test_snapshot_updates_multiple(files: Path, pytest_command: Command):
    expected_path = "pytest_files/test_snapshot0"
    first_file = files / expected_path / "out.exp"
    second_file = files / expected_path / "err.exp"
    res = pytest_command.run().ok()
    assert "test_snapshot/test_snapshot.py::test_snapshot SKIPPED (Updated" in res.stdout_plain
    assert first_file.read_text() == "plush plush"
    assert second_file.read_text() == "blobhaj"


@pytest.mark.parametrize(
    ("files", "pytest_command"),
    [
        (
            _get_f2_snapshot_files(
                {
                    "functional2": {
                        "test_snapshot": {
                            "test_snapshot.py": File(
                                dedent("""
                                    def test_snapshot(snapshot, tmp_path):
                                        (tmp_path / "updated.txt").write_text("snek plush")
                                        (tmp_path / "out.exp").symlink_to("./updated.txt")
                                        assert snapshot("out.exp") == "plush plush"
                                    """)
                            )
                        }
                    }
                }
            ),
            (["--accept-tests"], False),
        )
    ],
    indirect=True,
)
def test_snapshot_updates_no_location_when_symlink(files: Path, pytest_command: Command):
    expected_path = "pytest_files/test_snapshot0/updated.txt"
    output_file = files / expected_path
    res = pytest_command.run().ok()
    assert output_file.read_text() == "plush plush"
    assert "the updated file can be found here" not in res.stdout_plain
