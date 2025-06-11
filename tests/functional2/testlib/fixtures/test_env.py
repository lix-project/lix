import dataclasses
import re
import shutil
import sys
from pathlib import Path

import pytest

from functional2.testlib.fixtures.env import ManagedEnv, _ManagedPath


def test_env_inits_defaults(tmp_path: Path):
    env = ManagedEnv(tmp_path)
    assert env.get_env("GIT_CONFIG_SYSTEM") == "/dev/null"
    if sys.platform != "darwin":
        # Darwin doesn't like sandboxes so we don't have a sandbox shell here
        assert "busybox" in env.get_env("SHELL")
    else:
        assert "/bin/sh" in env.get_env("SHELL")
    assert env.get_env("PAGER") == "cat"

    assert env.dirs.test_root == tmp_path
    assert env.dirs.home == tmp_path / "test-home"
    assert env.dirs.nix_store_dir == tmp_path / "nix/store"


def test_env_unknown_none(env: ManagedEnv):
    assert env.get_env("SOME_UNKNOWN_STUFF") is None


def test_env_unknown_default(env: ManagedEnv):
    assert env.get_env("QUESTION_OF_LIFE", "42") == "42"


def test_env_sets(env: ManagedEnv):
    assert env.get_env("DRGN") is None
    env.set_env("DRGN", "cute")
    assert env.get_env("DRGN") == "cute"


def test_env_setitem_success(env: ManagedEnv):
    env["HELLO"] = "world"
    assert env.get_env("HELLO") == "world"


def test_env_getitem_known(env: ManagedEnv):
    env["HELLO"] = "world"
    assert env["HELLO"] == "world"


def test_env_getitem_unknown(env: ManagedEnv):
    with pytest.raises(KeyError):
        _ = env["HELLO"]


def test_env_overrides_custom(env: ManagedEnv):
    env.set_env("VALID", "you")
    assert env.get_env("VALID") == "you"
    env.set_env("VALID", "every creature")
    assert env.get_env("VALID") == "every creature"


def test_env_overrides_defaults(env: ManagedEnv):
    assert env.get_env("PAGER") == "cat"
    env.set_env("PAGER", "bat")
    assert env.get_env("PAGER") == "bat"


def test_env_unset_custom(env: ManagedEnv):
    env.set_env("FAILURE", "me")
    assert env.get_env("FAILURE") == "me"
    env.unset_env("FAILURE")
    assert env.get_env("FAILURE") is None


def test_env_unset_default(env: ManagedEnv):
    assert env.get_env("PAGER") == "cat"
    env.unset_env("PAGER")
    assert env.get_env("PAGER") is None


def test_env_set_none_errors(env: ManagedEnv):
    with pytest.raises(ValueError, match=r".+env.unset_env.+"):
        env.set_env("PAGER", None)  # type: ignore


def test_env_set_no_dirs(env: ManagedEnv):
    with pytest.raises(ValueError, match=r"Overriding paths should be done .+"):
        env.set_env("HOME", "/home/zelda")


def test_env_get_dir_works(env: ManagedEnv):
    assert env.get_env("HOME") == env.dirs.home


def test_env_dirs_created(env: ManagedEnv):
    fields = dataclasses.asdict(env.dirs).values()
    assert len(fields) > 1
    for field in fields:
        field: Path
        assert field.exists()


def test_env_get_path_fails(env: ManagedEnv):
    with pytest.raises(ValueError, match=r".+env\.path.+"):
        _ = env.get_env("PATH")


def test_env_set_path_fails(env: ManagedEnv):
    with pytest.raises(ValueError, match=r".+env\.path.+"):
        env.set_env("PATH", "a:b")


def test_env_to_env(tmp_path: Path):
    env = ManagedEnv(tmp_path)
    assert set(env.to_env().keys()) == {
        "GIT_CONFIG_SYSTEM",
        "SHELL",
        "PAGER",
        "HOME",
        "TEST_ROOT",
        "NIX_LOG_DIR",
        "NIX_STATE_DIR",
        "NIX_CONF_DIR",
        "NIX_BIN_DIR",
        "NIX_STORE_DIR",
        "CACHE_DIR",
        "XDG_CACHE_HOME",
        "PATH",
        "BUILD_TEST_SHELL",
    } | ({"_NIX_TEST_NO_SANDBOX"} if sys.platform == "darwin" else set())


def test_path_inits_build_shell():
    path = _ManagedPath("/path/to/build_shell")
    assert len(path._path) == 1
    assert path._path[0] == "/path/to/build_shell"


def test_path_appends_at_end():
    path = _ManagedPath("/path/to/build_shell")
    path.append("other_path")
    assert len(path._path) == 2
    assert path._path[1] == "other_path"


def test_path_prepends_at_front():
    path = _ManagedPath("/path/to/build_shell")
    path.prepend("other/path")
    assert len(path._path) == 2
    assert path._path[0] == "other/path"


def test_remove_path_known():
    path = _ManagedPath("/path/to/build_shell")
    path.append("other/path")
    assert len(path._path) == 2
    path.remove_path("other/path")
    assert path._path == ["/path/to/build_shell"]


def test_remove_path_unknown():
    path = _ManagedPath("/path/to/build_shell")
    path.append("other/path")
    assert len(path._path) == 2
    with pytest.raises(ValueError, match=r".+ x not in list"):
        path.remove_path("/home/nest/builder")
    assert len(path._path) == 2


def test_path_add_program_resolves():
    path = _ManagedPath("/path/to/build_shell")
    pytest_path = shutil.which("pytest")
    path.add_program("pytest", False)
    assert len(path._path) == 2
    assert path._path[1] == pytest_path


def test_path_add_program_entire_dir():
    path = _ManagedPath("/path/to/build_shell")
    pytest_path = shutil.which("pytest")
    path.add_program("pytest", True)
    assert len(path._path) == 2
    actual = path._path[1]
    assert actual != pytest_path
    assert actual == pytest_path.rpartition("/")[0]


def test_path_remove_program_specific():
    path = _ManagedPath("/path/to/build_shell")
    path.add_program("pytest", False)
    assert len(path._path) == 2
    path.remove_program("pytest")
    assert len(path._path) == 1


def test_path_remove_program_associated():
    path = _ManagedPath("/path/to/build_shell")
    path.add_program("pytest", True)
    assert len(path._path) == 2
    path.remove_program("pytest")
    assert len(path._path) == 1


def test_path_to_path():
    path = _ManagedPath("/path/to/build_shell")
    path.prepend("pictures/latest/kate")
    path.prepend("out/bin/lix")
    path.append("fops/with/noms")
    assert path.to_path() == "out/bin/lix:pictures/latest/kate:/path/to/build_shell:fops/with/noms"


def test_path_sandbox_entire_store_package():
    path = _ManagedPath("/path/to/build_shell")
    path.add_program(shutil.which("pytest"))
    sb_paths = path.to_sandbox_paths()
    assert len(sb_paths) == 2
    assert sb_paths[0] == "/path/to/build_shell"
    assert re.fullmatch(r"^/nix/store/\w{32}-python3-3\.\d{1,2}\.\d{1,2}-[^/]+$", sb_paths[1])
