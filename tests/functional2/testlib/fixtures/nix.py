import contextlib
import copy
import dataclasses
import sys
from functools import partialmethod
from pathlib import Path
from typing import Any, Literal
from collections.abc import Callable, Generator
import shutil
import subprocess
import logging

import pytest

from testlib.fixtures.command import CommandResult, Command
from testlib.fixtures.env import ManagedEnv
from testlib.utils import is_value_of_type


type _NixSettingValue = str | int | list[str] | bool | None


def _serialise(value: Any) -> str:
    if is_value_of_type(value, list[str]):
        return " ".join(_serialise(e) for e in value)
    if is_value_of_type(value, bool):
        return "true" if value else "false"
    if is_value_of_type(value, str | int):
        return str(value)

    msg = f"Value is unsupported in nix config: {value!r}, must be {_NixSettingValue.__value__}"
    raise ValueError(msg)


class NixSettings:
    """Settings for invoking Nix"""

    def __init__(self):
        self._settings: dict[str, _NixSettingValue] = {
            # Running the test suite creates a lot of stores in the test root (somewhere under TMPDIR).
            # Obviously, they are not critical for system operation, so there is no need to reserve space.
            # The cleanup will only happen a couple of runs later, wasting space in the meantime.
            # Effectively disable this space reserve to reduce the waste considerably (by about 98%).
            "gc-reserved-space": 0,
            "show-trace": True,
            "sandbox": True,
            # explicitly disable substitution by default, otherwise we may attempt to contact
            # substituters and slow down many tests with pointless connection retry timeouts.
            "substituters": [],
            "extra-sandbox-paths": [],
            "extra-experimental-features": [],
            "extra-deprecated-features": [],
        }

    def __getattr__(self, attr: str) -> _NixSettingValue:
        if attr.startswith("__"):
            return super().__getattr__(attr)
        return self._settings[attr.replace("_", "-")]

    def __setattr__(self, attr: str, value: _NixSettingValue):
        if attr == "_settings":
            super().__setattr__(attr, value)
        else:
            self._settings[attr.replace("_", "-")] = value

    def __getitem__(self, attr: str) -> _NixSettingValue:
        return self._settings[attr]

    def __setitem__(self, attr: str, value: str):
        self._settings[attr] = value

    def add_xp_feature(self, *names: list[str]):
        self["extra-experimental-features"] += names

    def add_dp_feature(self, *names: list[str]):
        self["extra-deprecated-features"] += names

    def update(self, args: dict[str, _NixSettingValue] | None = None, **kwargs):
        """
        Overrides the settings with the given dict or kwargs.
        """
        self._settings.update(kwargs | (args or {}))

    def clone(self) -> "NixSettings":
        """
        shortcut to clone the settings to a new object
        """
        return self.with_settings()

    def with_settings(
        self, args: dict[str, _NixSettingValue] | None = None, **kwargs
    ) -> "NixSettings":
        """
        Copies the current settings into a new object, overriding the provided ones.

        :returns: A new Settings object with overridden settings
        """
        new_settings = NixSettings()
        new_settings._settings = copy.deepcopy(self._settings)
        new_settings.update(args, **kwargs)
        return new_settings

    def to_config(self, env: ManagedEnv) -> str:
        config = ""

        self["extra-sandbox-paths"] += env.path.to_sandbox_paths()

        def field_may(name: str, value: Any, serializer: Callable[[Any], str] = _serialise):
            nonlocal config
            if value is not None:
                config += f"{name} = {serializer(value)}\n"

        for name, value in self._settings.items():
            field_may(name, value)

        return config

    def to_env_overlay(self, env: ManagedEnv) -> None:
        cfg = self.to_config(env)
        (env.dirs.nix_conf_dir / "nix.conf").write_text(cfg)
        env.set_env("NIX_CONFIG", cfg)


@dataclasses.dataclass
class Nix:
    env: ManagedEnv
    logger: logging.Logger
    _settings: NixSettings | None = dataclasses.field(init=False, default=None)

    @property
    def _nix_executable(self) -> Path:
        if nix_bin_dir := self.env.dirs.nix_bin_dir:
            return Path(nix_bin_dir) / "nix"

        if from_path := shutil.which("nix"):
            return Path(from_path)

        raise ValueError(
            "Couldn't find a Nix command to execute! Set NIX_BIN_DIR or fix your environment"
        )

    @property
    def settings(self) -> NixSettings:
        """
        :return: the settings for the nix instance
        """
        if self._settings is None:
            self._settings = NixSettings()
            self._settings.store = f"local?root={self.env.dirs.test_root}"

        return self._settings

    def nix_cmd(
        self, argv: list[str], flake: bool = False, build: bool | Literal["auto"] = "auto"
    ) -> Command:
        """
        Constructs a NixCommand with the appropriate settings.
        :param build: if the executed command wants to build stuff. This is required due to darwin shenanigans. "auto" will try to autodetect, override using `True` or `False`. Has no effect on linux.
        """
        # Create a copy of settings to not have a writing side effect
        settings = self.settings.clone()
        if flake:
            settings.add_xp_feature("nix-command", "flakes")
        # FIXME(rootile): Darwin needs special handling here, as it does not support (non-root) chroots...
        #  Hence, it cannot build using a relocated store so we just use the local (aka global) store instead
        #  This is kinda ugly but what else can one do
        if sys.platform == "darwin":
            if build is True or (
                # argv[1:2] does not throw a key error on empty lists
                # hence not crashing this check on an empty `nix.nix([])` call
                build == "auto" and (argv[0] == "nix-build" or argv[1:2] == ["build"])
            ):
                settings.store = None
                self.env.dirs.nix_store_dir = self.env.dirs.real_store_dir

        settings.to_env_overlay(self.env)
        return Command(argv=argv, exe=self._nix_executable, _env=self.env)

    def nix(
        self,
        cmd: list[str],
        nix_exe: str = "nix",
        flake: bool = False,
        build: bool | Literal["auto"] = "auto",
    ) -> Command:
        return self.nix_cmd([nix_exe, *cmd], flake=flake, build=build)

    @contextlib.contextmanager
    def daemon(
        self,
        args: list[str] | None = None,
        settings: dict[str, _NixSettingValue] | None = None,
        **kwargs,
    ) -> "Nix":
        daemon = copy.deepcopy(self)
        daemon.logger = self.logger.getChild("daemon")
        daemon.settings["allowed-users"] = ["*"]
        daemon.settings["trusted-users"] = []
        daemon.settings.store = f"local?root={self.env.dirs.test_root}"
        daemon.settings.update(settings)

        sockets_dir = Path(daemon.env.dirs.nix_state_dir) / "daemon-socket"
        sockets = [sockets_dir / "socket"]
        for p in sockets:
            p.unlink(missing_ok=True)

        proc = daemon.nix(args or [], nix_exe="nix-daemon", **kwargs).start()

        def log_daemon_result(result: CommandResult | None, level: int):
            if result:
                daemon.logger.log(level, "daemon exited with code %i", result.rc)
                daemon.logger.log(level, "stdout: %s", result.stdout_s)
                daemon.logger.log(level, "stderr: %s", result.stderr_s)
            else:
                daemon.logger.error("daemon exited unexpectedly")

        # wait for daemon to come up. this may take a while under load.
        # we only test the *last* socket in the list because that's the
        # last one the daemon creates, once it's there the daemon is up
        while not sockets[-1].exists():
            if status := proc.wait(0.01):
                log_daemon_result(status, logging.ERROR)
                raise RuntimeError("daemon exited during startup")

        inner = copy.deepcopy(self)
        inner.settings.store = f"unix://{sockets[-1]}"  # missing multi socket support

        try:
            timeout, level = 1, logging.ERROR
            yield inner
            # 5 seconds should be enough to wait for a *graceful* exit.
            timeout, level = 5, logging.DEBUG
        finally:
            result = proc.terminate(timeout)
            if not result:
                result = proc.kill()
            log_daemon_result(result, level)

    # Mark each of these as correct as they are not ClassVars, but we also don't want to turn off RUF045
    nix_build = partialmethod(nix, nix_exe="nix-build")  # noqa: RUF045
    nix_shell = partialmethod(nix, nix_exe="nix-shell")  # noqa: RUF045
    nix_store = partialmethod(nix, nix_exe="nix-store")  # noqa: RUF045
    nix_env = partialmethod(nix, nix_exe="nix-env")  # noqa: RUF045
    nix_instantiate = partialmethod(nix, nix_exe="nix-instantiate")  # noqa: RUF045
    nix_channel = partialmethod(nix, nix_exe="nix-channel")  # noqa: RUF045
    nix_prefetch_url = partialmethod(nix, nix_exe="nix-prefetch-url")  # noqa: RUF045

    def eval(
        self, expr: str, settings: NixSettings | None = None, flags: list[str] | None = None
    ) -> CommandResult:
        """
        calls `nix eval --json --expr {expr}` using the given expression
        :param expr: what to evaluate
        :param settings: if none, the global settings will be used, otherwise the given one
        :param flags: if none, empty list, otherwise pass flags to the CLI invocation
        :return: result of the evaluation
        """
        if flags is None:
            flags = []
        orig = self.settings.clone()
        self._settings = settings or self.settings
        self.settings.add_xp_feature("nix-command")

        cmd = self.nix(["eval", "--json", *flags, "--expr", expr])
        # restore previous settings
        self._settings = orig
        return cmd.run()

    @property
    def store_dir(self) -> Path:
        """
        The actual NIX_STORE_DIR this Nix command uses.
        """
        assert self.env.dirs.real_store_dir is not None, "bug in ManagedEnv"
        return self.env.dirs.real_store_dir

    def physical_store_path_for(self, path: str | Path) -> Path:
        """
        Takes a /nix/store/… path and rewrites it to be relative to this Nix's NIX_STORE_DIR.

        Nix accepts and returns store paths as `/nix/store` even when that's not where `NIX_STORE_DIR`
        physically is on the filesystem. Since we move the conceptual root for Nix to `test_root`,
        these "virtual" paths differ from the physical ones. So this function will convert `/nix/store`
        "virtual" paths to their real, physical location on the system.

        Basically, if you're passing it to `nix build` or `nix-store` or whatever, you want the
        `/nix/store` version. If you're passing it to a Python API (like pathlib.Path.exists()) or a
        command that operates on arbitrary files instead of store paths, you want the output of this
        function.


        :param path: a string or Path to convert
        :return: a Path object holding the rewritten, physical system path to the store entry
        """
        return (
            Path(str(path).replace("/nix/store", self.store_dir.as_posix()))
            if str(path).startswith("/nix/store")
            else Path(path)
        )

    def hash_path(self, store_path: str | Path, *args: str) -> str:
        """
        Shortcut to use `nix hash path {store_path}`, converting "virtual" store paths returned
        from Nix to their physical system paths including the test root.

        :param store_path: store path of the derivation or entry to hash
        """
        actual_path = self.physical_store_path_for(store_path).as_posix()
        res = self.nix(["hash", "path", actual_path, *args], flake=True).run().ok()
        return res.stdout_plain

    def clear_store(self):
        """
        Clears the test-owned store (and state) and resets them to an empty state
        """
        nix_store_dir = self.env.dirs.real_store_dir
        state_dir = self.env.dirs.nix_state_dir

        # Make store writable
        Command(["chmod", "-R", "+w", nix_store_dir], self.env).run().ok()
        shutil.rmtree(nix_store_dir)
        shutil.rmtree(state_dir)

        # Re-create the directories
        nix_store_dir.mkdir()
        state_dir.mkdir()


_fully_sandboxed = (
    sys.platform == "linux"
    and Path("/proc/self/ns/user").is_symlink()
    and subprocess.run(["unshare", "--user", "--mount", "--pid", "true"]).returncode == 0
)


def pytest_runtest_setup(item: Any):
    for mark in item.iter_markers(name="full_sandbox"):
        if not _fully_sandboxed:
            pytest.skip(f"{sys.platform} does not support full sandboxing")


@pytest.fixture
def nix(tmp_path: Path, env: ManagedEnv, logger: logging.Logger) -> Generator[Nix, Any, None]:
    """
    Provides a rich way of calling `nix`.
    For pre-applied commands use `nix.nix_instantiate`, `nix.nix_build` etc.
    After configuring the command, use `.run()` to run it
    """
    yield Nix(env, logger)
    # when things are done using the nix store, the permissions for the store are read only
    # after the test was executed, we set the permissions to rwx (write being the important part)
    # for pytest to be able to delete the files during cleanup
    cmd = Command(argv=["chmod", "-R", "+w", str(tmp_path.absolute())], _env=env)
    cmd.run().ok()
