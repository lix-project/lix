import contextlib
import copy
import dataclasses
import sys
from functools import partialmethod
from pathlib import Path
from textwrap import dedent
from typing import Any, Literal
from collections.abc import Callable, Generator
import shutil
import subprocess
import logging

import pytest

from testlib.fixtures.command import CommandResult, Command
from testlib.fixtures.env import ManagedEnv
from testlib.utils import is_value_of_type


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

    other_settings: dict[str, str | int | list[str] | set[str | int] | None] = dataclasses.field(
        default_factory=lambda: {
            "show-trace": "true",
            "sandbox": "true",
            # explicitly disable substitution by default, otherwise we may attempt to contact
            # substituters and slow down many tests with pointless connection retry timeouts.
            "substituters": [],
        }
    )
    """
    All other `nix.conf` settings. `None` values are not written to the config.
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
            # Running the test suite creates a lot of stores in the test root (somewhere under TMPDIR).
            # Obviously, they are not critical for system operation, so there is no need to reserve space.
            # The cleanup will only happen a couple of runs later, wasting space in the meantime.
            # Effectively disable this space reserve to reduce the waste considerably (by about 98%).
            gc-reserved-space = 0
            extra-sandbox-paths = {" ".join(env.path.to_sandbox_paths())}
        """)
        # Note: newline at the end is required due to nix being nix;
        # FIXME(Jade): #953 this is annoying in the CLI too, we should fix it!

        def serialise(value: Any) -> str:
            # TODO(Commentator2.0): why exactly are ints supported?
            if is_value_of_type(value, set[str | int] | list[str | int]):
                return " ".join(serialise(e) for e in value)
            if is_value_of_type(value, str | int):
                return str(value)

            msg = f"Value is unsupported in nix config: {value!r}, must bei either `str|int` or `set[str|int]` or `list[str|int]`"
            raise ValueError(msg)

        def field_may(name: str, value: Any, serializer: Callable[[Any], str] = serialise):
            nonlocal config
            if value is not None:
                config += f"{name} = {serializer(value)}\n"

        for k, v in [("experimental-features", self.experimental_features), ("store", self.store)]:
            assert k not in self.other_settings
            field_may(k, v)
        for k, v in self.other_settings.items():
            field_may(k, v)
        assert self.store or self.nix_store_dir, (
            "Failing to set either nix_store_dir or store will cause accidental use of the system store."
        )
        return config

    def to_env_overlay(self, env: ManagedEnv) -> None:
        cfg = self.to_config(env)
        (env.dirs.nix_conf_dir / "nix.conf").write_text(cfg)
        env.set_env("NIX_CONFIG", cfg)
        if self.nix_store_dir:
            env.dirs.nix_store_dir = self.nix_store_dir


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
        settings = dataclasses.replace(self.settings)
        if flake:
            settings.feature("nix-command", "flakes")
        # FIXME(Commentator2.0): Darwin needs special handling here, as it does not support (non-root) chroots...
        #  Hence, it cannot build using a relocated store so we just use the local (aka global) store instead
        #  This is kinda ugly but what else can one do
        if sys.platform == "darwin":
            if build is True or (
                # argv[1:2] does not throw a key error on empty lists
                # hence not crashing this check on an empty `nix.nix([])` call
                build == "auto" and (argv[0] == "nix-build" or argv[1:2] == ["build"])
            ):
                settings.store = None
                settings.nix_store_dir = self.env.dirs.real_store_dir

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
        self, args: list[str] = [], settings: dict[str, str | list[str]] = {}, **kwargs
    ) -> "Nix":
        daemon = copy.deepcopy(self)
        daemon.settings.other_settings["allowed-users"] = ["*"]
        daemon.settings.other_settings["trusted-users"] = []
        daemon.settings.store = f"local?root={self.env.dirs.test_root}"
        daemon.settings.other_settings |= settings

        sockets_dir = Path(daemon.env.dirs.nix_state_dir) / "daemon-socket"
        sockets = [sockets_dir / "socket"]
        for p in sockets:
            p.unlink(missing_ok=True)

        proc = daemon.nix(args, nix_exe="nix-daemon", **kwargs).start()

        def log_daemon_result(result: CommandResult | None, level: int):
            if result:
                self.logger.log(level, "daemon exited with code %i", result.rc)
                self.logger.log(level, "stdout: %s", result.stdout_s)
                self.logger.log(level, "stderr: %s", result.stderr_s)
            else:
                self.logger.error("daemon exited unexpectedly")

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
        orig = dataclasses.replace(self.settings)
        self._settings = settings or self.settings
        self.settings.feature("nix-command")

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
