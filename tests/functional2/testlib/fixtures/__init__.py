import os
import json
import subprocess
from typing import Any
from pathlib import Path
from functools import partialmethod
from functional2.testlib.terminal_code_eater import eat_terminal_codes
import dataclasses


@dataclasses.dataclass
class CommandResult:
    cmd: list[str]
    rc: int
    """Return code"""
    stderr: bytes
    """Outputted stderr"""
    stdout: bytes
    """Outputted stdout"""

    def ok(self):
        if self.rc != 0:
            print('stdout:', self.stdout_s)
            print('stderr:', self.stderr_s)
            raise subprocess.CalledProcessError(returncode=self.rc,
                                                cmd=self.cmd,
                                                stderr=self.stderr,
                                                output=self.stdout)
        return self

    def expect(self, rc: int):
        if self.rc != rc:
            print('stdout:', self.stdout_s)
            print('stderr:', self.stderr_s)
            raise subprocess.CalledProcessError(returncode=self.rc,
                                                cmd=self.cmd,
                                                stderr=self.stderr,
                                                output=self.stdout)
        return self

    @property
    def stdout_s(self) -> str:
        """Command stdout as str"""
        return self.stdout.decode('utf-8', errors='replace')

    @property
    def stderr_s(self) -> str:
        """Command stderr as str"""
        return self.stderr.decode('utf-8', errors='replace')

    @property
    def stdout_plain(self) -> str:
        """Command stderr as str with terminal escape sequences eaten and whitespace stripped"""
        return eat_terminal_codes(self.stdout).decode('utf-8', errors='replace').strip()

    @property
    def stderr_plain(self) -> str:
        """Command stderr as str with terminal escape sequences eaten and whitespace stripped"""
        return eat_terminal_codes(self.stderr).decode('utf-8', errors='replace').strip()

    def json(self) -> Any:
        self.ok()
        return json.loads(self.stdout)


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

    def feature(self, *names: str):
        self.experimental_features = (self.experimental_features
                                      or set()) | set(names)
        return self

    def to_config(self) -> str:
        config = ''

        def serialise(value):
            if type(value) in {str, int}:
                return str(value)
            elif type(value) in {list, set}:
                return ' '.join(str(e) for e in value)
            else:
                raise ValueError(
                    f'Value is unsupported in nix config: {value!r}')

        def field_may(name, value, serialiser=serialise):
            nonlocal config
            if value is not None:
                config += f'{name} = {serialiser(value)}\n'

        field_may('experimental-features', self.experimental_features)
        field_may('store', self.store)
        assert self.store or self.nix_store_dir, 'Failing to set either nix_store_dir or store will cause accidental use of the system store.'
        return config

    def to_env_overlay(self) -> dict[str, str]:
        ret = {'NIX_CONFIG': self.to_config()}
        if self.nix_store_dir:
            ret['NIX_STORE_DIR'] = str(self.nix_store_dir)
        return ret


@dataclasses.dataclass
class Command:
    argv: list[str]
    env: dict[str, str] = dataclasses.field(default_factory=dict)
    stdin: bytes | None = None
    cwd: Path | None = None

    def with_env(self, **kwargs) -> 'Command':
        self.env.update(kwargs)
        return self

    def with_stdin(self, stdin: bytes) -> 'Command':
        self.stdin = stdin
        return self

    def run(self) -> CommandResult:
        proc = subprocess.Popen(
            self.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE if self.stdin else subprocess.DEVNULL,
            cwd=self.cwd,
            env=self.env,
        )
        (stdout, stderr) = proc.communicate(input=self.stdin)
        rc = proc.returncode
        return CommandResult(cmd=self.argv,
                             rc=rc,
                             stdout=stdout,
                             stderr=stderr)


@dataclasses.dataclass
class NixCommand(Command):
    settings: NixSettings = dataclasses.field(default_factory=NixSettings)

    def apply_nix_config(self):
        self.env.update(self.settings.to_env_overlay())

    def run(self) -> CommandResult:
        self.apply_nix_config()
        return super().run()


@dataclasses.dataclass
class Nix:
    test_root: Path

    def hermetic_env(self):
        # mirroring vars-and-functions.sh
        home = self.test_root / 'test-home'
        home.mkdir(parents=True, exist_ok=True)
        return {
            'NIX_LOCALSTATE_DIR': self.test_root / 'var',
            'NIX_LOG_DIR': self.test_root / 'var/log/nix',
            'NIX_STATE_DIR': self.test_root / 'var/nix',
            'NIX_CONF_DIR': self.test_root / 'etc',
            'NIX_DAEMON_SOCKET_PATH': self.test_root / 'daemon-socket',
            'NIX_USER_CONF_FILES': '',
            'HOME': home,
        }

    def make_env(self):
        # We conservatively assume that people might want to successfully get
        # some env through to the subprocess, so we override whatever is in the
        # global env.
        d = os.environ.copy()
        d.update(self.hermetic_env())
        return d

    def cmd(self, argv: list[str]):
        return Command(argv=argv, cwd=self.test_root, env=self.make_env())

    def settings(self, allow_builds: bool = False):
        """
        Parameters:
        - allow_builds: relocate the Nix store so that builds work (however, makes store paths non-reproducible across test runs!)
        """
        settings = NixSettings()
        store_path = self.test_root / 'store'
        if allow_builds:
            settings.nix_store_dir = store_path
        else:
            settings.store = str(store_path)
        return settings

    def nix_cmd(self, argv: list[str], flake: bool = False):
        """
        Constructs a NixCommand with the appropriate settings.
        """
        settings = self.settings()
        if flake:
            settings.feature('nix-command', 'flakes')

        return NixCommand(argv=argv,
                          cwd=self.test_root,
                          env=self.make_env(),
                          settings=settings)

    def nix(self, cmd: list[str], nix_exe: str = 'nix', flake: bool = False) -> NixCommand:
        return self.nix_cmd([nix_exe, *cmd], flake=flake)

    nix_build = partialmethod(nix, nix_exe='nix-build')
    nix_shell = partialmethod(nix, nix_exe='nix-shell')
    nix_store = partialmethod(nix, nix_exe='nix-store')
    nix_env = partialmethod(nix, nix_exe='nix-env')
    nix_instantiate = partialmethod(nix, nix_exe='nix-instantiate')
    nix_channel = partialmethod(nix, nix_exe='nix-channel')
    nix_prefetch_url = partialmethod(nix, nix_exe='nix-prefetch-url')

    def eval(
        self, expr: str,
        settings: NixSettings | None = None) -> CommandResult:
        # clone due to reference-shenanigans
        settings = dataclasses.replace(settings or self.settings()).feature('nix-command')

        cmd = self.nix(['eval', '--json', '--expr', expr])
        cmd.settings = settings
        return cmd.run()
