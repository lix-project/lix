import dataclasses
import os
from functools import partialmethod
from pathlib import Path
from typing import Any, AnyStr
from collections.abc import Callable, Generator

import pytest

from functional2.testlib.commands import CommandResult, Command


@dataclasses.dataclass
class NixSettings:
    """Settings for invoking Nix"""

    experimental_features: set[str] | None = None
    store: str | None = None
    """
    The store to operate on (may be a path or other thing, see nix help-stores).

    Note that this can be set to the test's store directory if you want to use
    /nix/store paths inside that test rather than NIX_STORE_DIR renaming
    /nix/store to some unstable name (assuming that no builds are invoked).
    """
    nix_store_dir: Path | None = None
    """
    Alternative name to use for /nix/store: breaks all references and NAR imports if
    set, but does allow builds in tests (since builds do not require chroots if
    the store is relocated).
    """

    def feature(self, *names: str) -> "NixSettings":
        """
        Adds the given features to the list of enabled `experimental_features`
        :param names: feature names to enable
        :return: self, command is chainable
        """
        self.experimental_features = (self.experimental_features or set()) | set(names)
        return self

    def to_config(self) -> str:
        config = ""

        def serialise(value: Any) -> str:
            if type(value) in {str, int}:
                return str(value)
            if type(value) in {list, set}:
                return " ".join(str(e) for e in value)
            msg = f"Value is unsupported in nix config: {value!r}"
            raise ValueError(msg)

        def field_may(name: str, value: Any, serializer: Callable[[Any], str] = serialise):
            nonlocal config
            if value is not None:
                config += f"{name} = {serializer(value)}\n"

        field_may("experimental-features", self.experimental_features)
        field_may("store", self.store)
        assert (
            self.store or self.nix_store_dir
        ), "Failing to set either nix_store_dir or store will cause accidental use of the system store."
        return config

    def to_env_overlay(self) -> dict[str, str]:
        ret = {"NIX_CONFIG": self.to_config()}
        if self.nix_store_dir:
            ret["NIX_STORE_DIR"] = str(self.nix_store_dir)
        return ret


@dataclasses.dataclass
class NixCommand(Command):
    """
    Custom Command class which applies the given NixSettings before the command is run
    """

    settings: NixSettings = dataclasses.field(default_factory=NixSettings)

    def apply_nix_config(self):
        self.env.update(self.settings.to_env_overlay())

    def run(self) -> CommandResult:
        self.apply_nix_config()
        return super().run()


@dataclasses.dataclass
class Nix:
    test_root: Path

    def hermetic_env(self) -> dict[str, Path]:
        # mirroring vars-and-functions.sh
        home = self.test_root / "test-home"
        home.mkdir(parents=True, exist_ok=True)
        return {
            "NIX_LOCALSTATE_DIR": self.test_root / "var",
            "NIX_LOG_DIR": self.test_root / "var/log/nix",
            "NIX_STATE_DIR": self.test_root / "var/nix",
            "NIX_CONF_DIR": self.test_root / "etc",
            "NIX_DAEMON_SOCKET_PATH": self.test_root / "daemon-socket",
            "NIX_USER_CONF_FILES": "",
            "HOME": home,
        }

    def make_env(self) -> dict[AnyStr, AnyStr]:
        # We conservatively assume that people might want to successfully get
        # some env through to the subprocess, so we override whatever is in the
        # global env.
        d = os.environ.copy()
        d.update(self.hermetic_env())
        return d

    def cmd(self, argv: list[str]) -> Command:
        return Command(argv=argv, cwd=self.test_root, env=self.make_env())

    def settings(self, allow_builds: bool = False) -> NixSettings:
        """
        Parameters:
        - allow_builds: relocate the Nix store so that builds work (however, makes store paths non-reproducible across test runs!)
        """
        settings = NixSettings()
        store_path = self.test_root / "store"
        if allow_builds:
            settings.nix_store_dir = store_path
        else:
            settings.store = str(store_path)
        return settings

    def nix_cmd(self, argv: list[str], flake: bool = False) -> NixCommand:
        """
        Constructs a NixCommand with the appropriate settings.
        """
        settings = self.settings()
        if flake:
            settings.feature("nix-command", "flakes")

        return NixCommand(argv=argv, cwd=self.test_root, env=self.make_env(), settings=settings)

    def nix(self, cmd: list[str], nix_exe: str = "nix", flake: bool = False) -> NixCommand:
        return self.nix_cmd([nix_exe, *cmd], flake=flake)

    nix_build = partialmethod(nix, nix_exe="nix-build")
    nix_shell = partialmethod(nix, nix_exe="nix-shell")
    nix_store = partialmethod(nix, nix_exe="nix-store")
    nix_env = partialmethod(nix, nix_exe="nix-env")
    nix_instantiate = partialmethod(nix, nix_exe="nix-instantiate")
    nix_channel = partialmethod(nix, nix_exe="nix-channel")
    nix_prefetch_url = partialmethod(nix, nix_exe="nix-prefetch-url")

    def eval(self, expr: str, settings: NixSettings | None = None) -> CommandResult:
        if settings is None:
            settings = self.settings()
        # clone due to reference-shenanigans
        settings = dataclasses.replace(settings).feature("nix-command")

        cmd = self.nix(["eval", "--json", "--expr", expr])
        cmd.settings = settings
        return cmd.run()


@pytest.fixture
def nix(tmp_path: Path) -> Generator[Nix, Any, None]:
    """
    Provides a rich way of calling `nix`.
    For pre-applied commands use `nix.nix_instantiate`, `nix.nix_build` etc.
    After configuring the command, use `.run()` to run it
    """
    yield Nix(tmp_path)
    # when things are done using the nix store, the permissions for the store are read only
    # after the test was executed, we set the permissions to rwx (write being the important part)
    # for pytest to be able to delete the files during cleanup
    Command(argv=["chmod", "-R", "+w", str(tmp_path.absolute())], env=os.environ.copy()).run().ok()
