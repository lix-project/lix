import dataclasses
import sys
from functools import partialmethod
from pathlib import Path
from textwrap import dedent
from typing import Any, Literal
from collections.abc import Callable, Generator

import pytest

from functional2.testlib.fixtures.command import CommandResult, Command
from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.utils import is_value_of_type


@dataclasses.dataclass
class NixSettings:
    """Settings for invoking Nix"""

    experimental_features: set[str] | None = None
    store: str | None = None
    """
    The store to operate on (may be a path or other thing, see `nix help-stores`).

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

    def to_config(self, env: ManagedEnv) -> str:
        config = dedent(f"""
            show-trace = true
            sandbox = true
            # explicitly disable substitution by default, otherwise we may attempt to contact
            # substituters and slow down many tests with pointless connection retry timeouts.
            substituters =
            extra-sandbox-paths = {" ".join(env.path.to_sandbox_paths())}
        """)
        # Note: newline at the end is required due to nix being nix;
        # FIXME(Jade): #953 this is annoying in the CLI too, we should fix it!

        def serialise(value: Any) -> str:
            # TODO(Commentator2.0): why exactly are ints supported?
            if is_value_of_type(value, set[str | int]):
                return " ".join(serialise(e) for e in value)
            if is_value_of_type(value, str | int):
                return str(value)

            msg = f"Value is unsupported in nix config: {value!r}, must bei either `str|int` or `set[str|int]`"
            raise ValueError(msg)

        def field_may(name: str, value: Any, serializer: Callable[[Any], str] = serialise):
            nonlocal config
            if value is not None:
                config += f"{name} = {serializer(value)}\n"

        field_may("experimental-features", self.experimental_features)
        field_may("store", self.store)
        assert self.store or self.nix_store_dir, (
            "Failing to set either nix_store_dir or store will cause accidental use of the system store."
        )
        return config

    def to_env_overlay(self, env: ManagedEnv) -> None:
        cfg = self.to_config(env)
        (env.dirs.nix_conf_dir / "nix.conf").write_text(cfg)
        env.set_env("NIX_CONFIG", cfg)
        if self.nix_store_dir:
            env.dirs.nix_store_dir = str(self.nix_store_dir)


@dataclasses.dataclass
class Nix:
    env: ManagedEnv
    _settings: NixSettings | None = dataclasses.field(init=False, default=None)

    @property
    def settings(self) -> NixSettings:
        """
        :return: the settings for the nix instance
        """
        if self._settings is None:
            self._settings = NixSettings()
            self._settings.store = f"local?root={self.env.dirs.test_root}&store=/nix/store"

        return self._settings

    def nix_cmd(
        self, argv: list[str], flake: bool = False, build: bool | Literal["auto"] = "auto"
    ) -> Command:
        """
        Constructs a NixCommand with the appropriate settings.
        :param build: if the executed command wants to build stuff. This is required due to darwin shenanigans. "auto" will try to autodetect, override using `True` or `False`. Has no effect on linux.
        """
        # Create a copy of settings to not have a writing side effect
        settings = dataclasses.replace(self.settings)
        if flake:
            settings.feature("nix-command", "flakes")
        # FIXME(Commentator2.0): Darwin needs special handling here, as it does not support (non-root) chroots...
        #  Hence, it cannot build using a relocated store so we just use the local (aka global) store instead
        #  This is kinda ugly but what else can one do
        if sys.platform == "darwin":
            if build is True or (
                build == "auto" and (argv[0] == "nix-build" or argv[1] == "build")
            ):
                settings.store = None
                settings.nix_store_dir = self.env.dirs.nix_store_dir

        settings.to_env_overlay(self.env)
        return Command(argv=argv, _env=self.env)

    def nix(
        self,
        cmd: list[str],
        nix_exe: str = "nix",
        flake: bool = False,
        build: bool | Literal["auto"] = "auto",
    ) -> Command:
        return self.nix_cmd([nix_exe, *cmd], flake=flake, build=build)

    # Mark each of these as correct as they are not ClassVars, but we also don't want to turn off RUF045
    nix_build = partialmethod(nix, nix_exe="nix-build")  # noqa: RUF045
    nix_shell = partialmethod(nix, nix_exe="nix-shell")  # noqa: RUF045
    nix_store = partialmethod(nix, nix_exe="nix-store")  # noqa: RUF045
    nix_env = partialmethod(nix, nix_exe="nix-env")  # noqa: RUF045
    nix_instantiate = partialmethod(nix, nix_exe="nix-instantiate")  # noqa: RUF045
    nix_channel = partialmethod(nix, nix_exe="nix-channel")  # noqa: RUF045
    nix_prefetch_url = partialmethod(nix, nix_exe="nix-prefetch-url")  # noqa: RUF045

    def eval(self, expr: str, settings: NixSettings | None = None) -> CommandResult:
        """
        calls `nix eval --json --expr {expr}` using the given expression
        :param expr: what to evaluate
        :param settings: if none, the global settings will be used, otherwise the given one
        :return: result of the evaluation
        """
        orig = dataclasses.replace(self.settings)
        self._settings = settings or self.settings
        self.settings.feature("nix-command")

        cmd = self.nix(["eval", "--json", "--expr", expr])
        # restore previous settings
        self._settings = orig
        return cmd.run()


@pytest.fixture
def nix(tmp_path: Path, env: ManagedEnv) -> Generator[Nix, Any, None]:
    """
    Provides a rich way of calling `nix`.
    For pre-applied commands use `nix.nix_instantiate`, `nix.nix_build` etc.
    After configuring the command, use `.run()` to run it
    """
    yield Nix(env)
    # when things are done using the nix store, the permissions for the store are read only
    # after the test was executed, we set the permissions to rwx (write being the important part)
    # for pytest to be able to delete the files during cleanup
    cmd = Command(argv=["chmod", "-R", "+w", str(tmp_path.absolute())], _env=env)
    cmd.run().ok()
