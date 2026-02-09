from pathlib import Path

import pytest

from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.nix import NixSettings


def test_nix_settings_set_item():
    settings = NixSettings()
    settings["hello"] = "world"

    assert settings._settings["hello"] == "world"


def test_nix_settings_get_item():
    settings = NixSettings()
    assert settings["substituters"] == []


def test_nix_settings_set_attr():
    settings = NixSettings()
    settings.cores = 5

    assert settings._settings["cores"] == 5


def test_nix_settings_get_attr():
    settings = NixSettings()
    assert settings.sandbox is True


def test_nix_settings_get_attr_underscore():
    settings = NixSettings()
    assert settings.extra_deprecated_features == []

    settings["extra-deprecated-features"] += ["ancient-let"]

    assert settings.extra_deprecated_features == ["ancient-let"]


def test_nix_settings_set_attr_underscore():
    settings = NixSettings()
    assert settings["extra-deprecated-features"] == []

    settings.extra_deprecated_features += ["ancient-let"]
    assert settings["extra-deprecated-features"] == ["ancient-let"]


def test_nix_settings_update_replaces():
    settings = NixSettings()
    assert settings.sandbox is True

    settings.update({"sandbox": False})
    assert settings.sandbox is False

    settings.update(sandbox=True)
    assert settings.sandbox is True


def test_nix_settings_with_doesnt_effect_orig():
    orig = NixSettings()
    orig["extra-experimental-features"] += ["nix-command"]

    new = orig.with_settings({"extra-experimental-features": ["some-feature"]})
    assert new["extra-experimental-features"] == ["some-feature"]
    assert orig["extra-experimental-features"] == ["nix-command"]


def test_nix_settings_serializes_xf(env: ManagedEnv):
    settings = NixSettings()
    settings["extra-experimental-features"] += ["a", "b"]

    assert "extra-experimental-features = a b\n" in settings.to_config(env)


def test_nix_settings_serializes_store(env: ManagedEnv):
    settings = NixSettings()
    settings.store = "local?root=/some/path"

    assert "store = local?root=/some/path\n" in settings.to_config(env)


def test_nix_settings_serializes_both(env: ManagedEnv):
    settings = NixSettings()
    settings["extra-experimental-features"] += ["a", "b"]
    settings.store = "local?root=/some/path"

    serialized = settings.to_config(env)

    assert "extra-experimental-features = a b\n" in serialized
    assert "store = local?root=/some/path" in serialized


def test_nix_settings_ser_fails_bad_top_level_type(env: ManagedEnv):
    settings = NixSettings()
    settings.experimental_features = {"a": "b"}  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match=r"Value is unsupported in nix config: {'a': 'b'}"):
        settings.to_config(env)


def test_nix_settings_ser_fails_bad_sub_type(env: ManagedEnv):
    settings = NixSettings()
    settings.experimental_features = [["a", "b"], "c"]  # type: ignore we are testing the types here

    with pytest.raises(ValueError, match=r"Value is unsupported in nix config: .+"):
        settings.to_config(env)


def test_nix_settings_to_env_overlay_no_store_dir(tmp_path: Path):
    env = ManagedEnv(tmp_path)
    settings = NixSettings()
    settings.store = "local?root=/some/path"

    settings.to_env_overlay(env)
    assert "store = local?root=/some/path\n" in env._env["NIX_CONFIG"]
