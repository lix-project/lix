from pathlib import Path
from textwrap import dedent

import pytest
from functional2.testlib.fixtures.file_helper import (
    CopyFile,
    CopyTemplate,
    CopyTree,
    File,
    Symlink,
    AssetSymlink,
    merge_file_declaration,
    with_files,
)


@with_files({"test_file_1": File("this is some test content")})
def test_file_creates(files: Path):
    file_name = "test_file_1"
    file_content = "this is some test content"

    target_path = files / file_name
    assert target_path.exists()
    assert target_path.read_text() == file_content


@with_files({"test_file_2": File("asdf")})
def test_no_file_leakage(files: Path):
    file_name = "test_file_2"
    file_content = "asdf"

    some_other_file_name = "test_file_1"

    assert (files / file_name).exists()
    assert (files / file_name).read_text() == file_content
    # Check for no leakage from other tests
    assert not (files / some_other_file_name).exists()


@with_files({"copy_file_test.txt": CopyFile("assets/test_file_helper/copy_file_test.txt")})
def test_copy_file_no_name(files: Path):
    target_path = files / "copy_file_test.txt"

    assert target_path.exists()

    assert target_path.read_text() == "some amazing content\n"


@with_files({"new_name.txt": CopyFile("assets/test_file_helper/copy_file_test.txt")})
def test_copy_file_with_name(files: Path):
    assert (files / "new_name.txt").exists()
    assert not (files / "copy_file_test.txt").exists()


@with_files(
    {
        "target.txt": CopyTemplate(
            "assets/test_file_helper/copy_file_template.template",
            {"some_key": "abc", "other_key": 123, "key_in_braces": "in_braces"},
        )
    }
)
def test_template(files: Path):
    assert (files / "target.txt").exists()

    expected_content = dedent("""
        some_key is set to abc
        more of abc
        other_key is 123
        @not_a_key@
        @in_braces@
    """)

    actual_content = (files / "target.txt").read_text()
    assert actual_content == expected_content


@with_files(
    {
        "target.txt": CopyTemplate(
            "assets/test_file_helper/copy_file_template.template",
            {"some_key": "abc", "key_in_braces": "in_braces"},
        )
    }
)
@pytest.mark.xfail(raises=KeyError)
def test_template_missing_key(files: Path):
    # Empty, because the initialization of the CopyTemplate fails. Caught and tested for by the xfail mark
    ...


@with_files(
    {
        "target.txt": CopyTemplate(
            "assets/test_file_helper/copy_file_template.template",
            {
                "some_key": "Eragon",
                "key_in_braces": "in_braces",
                "other_key": "Arthur Leywin",
                "the beginning": "after the end",
            },
        )
    }
)
@pytest.mark.xfail(raises=ExceptionGroup)
def test_template_too_many_keys(files: Path):
    # Empty, because the initialization of the CopyTemplate fails. Caught and tested for by the xfail mark
    ...


@with_files({"some_folder": CopyTree("assets/test_file_helper/test_folder")})
def test_copy_tree(files: Path):
    files = files / "some_folder"
    assert (files / "a.txt").exists()
    assert (files / "b.txt").exists()
    assert (files / "sub_test").exists()
    assert (files / "sub_test" / "c.txt").exists()


@with_files(
    {
        "a.txt": File("Hello World"),
        "assembler.txt": File("reinforced iron plates\n"),
        "folder_1": {
            "empty.nix": File(""),
            "folder_2": {"not_empty.py": File("print('Arrruuuuuuuuuuuu')")},
        },
    }
)
def test_files_sub_dirs(files: Path):
    f = files / "a.txt"
    assert f.exists()
    assert f.read_text() == "Hello World"

    f = files / "assembler.txt"
    assert f.exists()

    fldr = files / "folder_1"
    assert fldr.exists()
    f = fldr / "empty.nix"
    assert f.exists()

    fldr /= "folder_2"
    assert fldr.exists()

    f = fldr / "not_empty.py"
    assert f.exists()


@with_files({"not_empty": {"a.txt": File("Hello World")}, "empty": {}})
def test_creates_empty_directory(files: Path):
    assert (files / "not_empty").exists()
    assert (files / "empty").exists()
    assert (files / "empty").is_dir()


@with_files({"a.sh": File("echo test", mode=0o477)})
def test_mode_setting(files: Path):
    file = files / "a.sh"
    assert file.exists()
    assert file.stat().st_mode & 0o477 == 0o477


@with_files({"a": AssetSymlink("assets/test_file_helper/copy_file_test.txt")})
def test_asset_symlink(files: Path):
    file = files / "a"
    assert file.exists(follow_symlinks=False)

    assert file.is_symlink()

    assert file.readlink().exists()

    assert file.read_text() == "some amazing content\n"


@with_files({"a": AssetSymlink("assets/test_file_helper/test_folder")})
def test_dir_symlink(files: Path):
    folder = files / "a"
    assert folder.exists(follow_symlinks=False)

    assert folder.is_symlink()

    assert folder.readlink().exists()

    assert folder.readlink().is_dir()

    assert (folder / "a.txt").exists()


@with_files({"a": AssetSymlink("assets/test_file_helper/this_does_not_exist")})
def test_invalid_asset_symlink(files: Path):
    link = files / "a"
    assert link.exists(follow_symlinks=False)
    assert link.is_symlink()
    assert not link.readlink().exists()


@pytest.mark.xfail(raises=ValueError)
@with_files({"a": AssetSymlink("/absolute/assets/test_file_helper/this_does_not_exist")})
def test_absolute_asset_symlink(files: Path):
    pass


@with_files({"tg": File("Hello World"), "folder": {"link": Symlink("../tg")}})
def test_file_symlink(files: Path):
    assert (files / "tg").exists()
    link = files / "folder" / "link"
    assert link.exists(follow_symlinks=False)
    assert link.is_symlink()
    # check that this is actually relative and not an absolute path
    assert str(link.readlink()) == "../tg"


@with_files({"test-file.txt": File("a")}, {"test-file.txt": File("b")})
def test_multiple_file_sets(files: Path):
    file = files / "test-file.txt"
    assert file.exists()
    assert file.read_text() == "a" or file.read_text() == "b"


def test_merge_fd_merges_correctly():
    fa = File("a")
    fc = File("c")
    fd = File("d")
    ff = File("f")
    fd1 = {"a": fa, "b": {"c": fc}}

    fd2 = {"b": {"d": fd}, "e": {"f": ff}}

    expected = {"a": fa, "b": {"c": fc, "d": fd}, "e": {"f": ff}}

    result = merge_file_declaration(fd1, fd2)
    assert result == expected


def test_merge_fd_throws_on_conflict():
    fd1 = {"a": {"b": File("b")}}
    fd2 = {"a": File("a")}

    with pytest.raises(
        ValueError,
        match=r"\('Cannot merge files; got two different values for the same path', 'a'\)",
    ):
        merge_file_declaration(fd1, fd2)


def test_merge_fd_throws_on_conflict_with_full_path():
    fd1 = {"a": {"b": {"c": {"d": File("d")}}}}
    fd2 = {"a": {"b": {"c": {"d": File("different file")}}}}

    with pytest.raises(
        ValueError,
        match=r"\('Cannot merge files; got two different values for the same path', 'a/b/c/d'\)",
    ):
        merge_file_declaration(fd1, fd2)
