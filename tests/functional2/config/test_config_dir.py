from testlib.fixtures.file_helper import File
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from pathlib import Path
import pytest

pytestmark = pytest.mark.no_daemon


def test_home_dot_config_no_xdg(nix: Nix, files: Path):
    # Test that using XDG_CONFIG_HOME works
    # Assert the config folder didn't exist initially.
    config_path = files / ".config"
    assert not config_path.exists()

    # Without XDG_CONFIG_HOME, creates $HOME/.config
    # Run against the nix registry to create the config dir
    # (Tip: this relies on removing non-existent entries being a no-op!)
    nix.nix(["registry", "remove", "userhome-without-xdg"], flake=True).run().ok()
    assert config_path.exists()


def test_config_xdg(nix: Nix, files: Path):
    config_path = files / ".config"
    xdg_config_path = files / ".xdg-config"

    nix.env["XDG_CONFIG_HOME"] = str(xdg_config_path)

    assert not config_path.exists()
    assert not xdg_config_path.exists()

    nix.nix(["registry", "remove", "userhome-without-xdg"], flake=True).run().ok()

    assert not config_path.exists()
    assert xdg_config_path.exists()
    assert (xdg_config_path / "nix").exists()


def test_load_files_xdg(nix: Nix, files: Path):
    # Test that files are loaded from XDG by default
    xdg_config_path = files / ".xdg-config"
    nix.env["XDG_CONFIG_HOME"] = str(xdg_config_path)
    nix.env["XDG_CONFIG_DIRS"] = f"{files}/dir1:{files}/dir2"

    res = nix.nix_build(["-v", "--version"]).run().ok()
    clean = res.stdout_plain.replace(str(files.parent), "/PWD")
    assert (
        "User configuration files: /PWD/test-home/.xdg-config/nix/nix.conf:/PWD/test-home/dir1/nix/nix.conf:/PWD/test-home/dir2/nix/nix.conf"
        in clean
    )


def test_user_conf_overrides(nix: Nix, files: Path):
    conf_files = f"{files}/file1.conf:{files}/file2.conf"
    nix.env["NIX_USER_CONF_FILES"] = conf_files
    res = nix.nix_build(["-v", "--version"]).run().ok()
    clean = res.stdout_plain.replace(str(files.parent), "/PWD")
    assert "User configuration files: /PWD/test-home/file1.conf:/PWD/test-home/file2.conf" in clean


@with_files(
    {
        "my-config": {
            "my-nix.conf": File(
                "experimental-features = nix-command\nsubstituters = https://example.com"
            )
        }
    }
)
def test_conf_load_custom_location(nix: Nix, files: Path):
    nix.env["NIX_USER_CONF_FILES"] = f"{files}/my-config/my-nix.conf"
    nix.settings.disabled = True
    res = nix.nix(["config", "show"]).run().ok()
    assert "substituters = https://example.com" in res.stdout_plain
