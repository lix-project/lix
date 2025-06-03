from pathlib import Path

import pytest

from functional2.testlib.fixtures.nix import NixSettings


def test_nix_settings_serializes_xf():
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.feature("a", "b")

    expected = "experimental-features = a b\n"
    assert settings.to_config() == expected


def test_nix_settings_serializes_store():
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.store = "some/path"

    expected = "store = some/path\n"
    assert settings.to_config() == expected


def test_nix_settings_serializes_both():
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.feature("a", "b")
    settings.store = "some/path"

    expected = "experimental-features = a b\nstore = some/path\n"
    assert settings.to_config() == expected


def test_nix_settings_ser_fails_bad_top_level_type():
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.experimental_features = {"a": "b"}  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match="Value is unsupported in nix config: {'a': 'b'}"):
        settings.to_config()


def test_nix_settings_ser_fails_bad_sub_type():
    settings = NixSettings(nix_store_dir=Path("/store/nix"))
    settings.experimental_features = [["a", "b"], "c"]  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match="Value is unsupported in nix config: .+"):
        settings.to_config()


def test_nix_settings_fails_without_store_and_store_dir():
    settings = NixSettings()

    with pytest.raises(
        AssertionError,
        match="Failing to set either nix_store_dir or store will cause accidental use of the system store.",
    ):
        settings.to_config()


def test_nix_settings_to_env_overlay_no_store_dir():
    settings = NixSettings()
    settings.store = "some/path"

    expected = {"NIX_CONFIG": "store = some/path\n"}
    assert settings.to_env_overlay() == expected


def test_nix_settings_to_env_overlay_store_dir():
    settings = NixSettings()
    settings.nix_store_dir = Path("/some/path")

    expected = {"NIX_CONFIG": "", "NIX_STORE_DIR": "/some/path"}
    assert settings.to_env_overlay() == expected
