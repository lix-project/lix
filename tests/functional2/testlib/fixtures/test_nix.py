import re
from pathlib import Path

import pytest

from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.fixtures.nix import NixSettings


def test_nix_settings_serializes_xf(env: ManagedEnv):
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.feature("a", "b")

    expected = r"(.|\n)*experimental-features = (a|b) (a|b)\n(.|\n)*"
    assert re.fullmatch(expected, settings.to_config(env), re.MULTILINE)


def test_nix_settings_serializes_store(env: ManagedEnv):
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.store = "some/path"

    expected = "store = some/path\n"
    assert expected in settings.to_config(env)


def test_nix_settings_serializes_both(env: ManagedEnv):
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.feature("a", "b")
    settings.store = "some/path"

    expected = "experimental-features = a b\nstore = some/path\n"
    assert expected in settings.to_config(env)


def test_nix_settings_ser_fails_bad_top_level_type(env: ManagedEnv):
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.experimental_features = {"a": "b"}  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match=r"Value is unsupported in nix config: {'a': 'b'}"):
        settings.to_config(env)


def test_nix_settings_ser_fails_bad_sub_type(env: ManagedEnv):
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.experimental_features = [["a", "b"], "c"]  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match=r"Value is unsupported in nix config: .+"):
        settings.to_config(env)


def test_nix_settings_fails_without_store_and_store_dir(env: ManagedEnv):
    settings = NixSettings()

    with pytest.raises(
        AssertionError,
        match=r"Failing to set either nix_store_dir or store will cause accidental use of the system store.",
    ):
        settings.to_config(env)


def test_nix_settings_to_env_overlay_no_store_dir(tmp_path: Path):
    env = ManagedEnv(tmp_path)
    settings = NixSettings()
    settings.store = "some/path"

    settings.to_env_overlay(env)
    assert "store = some/path\n" in env._env["NIX_CONFIG"]


def test_nix_settings_to_env_overlay_store_dir(tmp_path: Path):
    env = ManagedEnv(tmp_path)
    settings = NixSettings()
    settings.nix_store_dir = Path("/some/path")

    settings.to_env_overlay(env)
    assert "store = " not in env._env["NIX_CONFIG"]
    assert env.dirs.nix_store_dir == "/some/path"
