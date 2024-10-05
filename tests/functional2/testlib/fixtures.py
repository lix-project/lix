import os
import json
import subprocess
from typing import Any
from pathlib import Path
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
            raise subprocess.CalledProcessError(returncode=self.rc,
                                                cmd=self.cmd,
                                                stderr=self.stderr,
                                                output=self.stdout)
        return self

    def json(self) -> Any:
        self.ok()
        return json.loads(self.stdout)


@dataclasses.dataclass
class NixSettings:
    """Settings for invoking Nix"""
    experimental_features: set[str] | None = None

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
        return config


@dataclasses.dataclass
class Nix:
    test_root: Path

    def hermetic_env(self):
        # mirroring vars-and-functions.sh
        home = self.test_root / 'test-home'
        home.mkdir(parents=True, exist_ok=True)
        return {
            'NIX_STORE_DIR': self.test_root / 'store',
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

    def call(self, cmd: list[str], extra_env: dict[str, str] = {}):
        """
        Calls a process in the test environment.
        """
        env = self.make_env()
        env.update(extra_env)
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=self.test_root,
            env=env,
        )
        (stdout, stderr) = proc.communicate()
        rc = proc.returncode
        return CommandResult(cmd=cmd, rc=rc, stdout=stdout, stderr=stderr)

    def nix(self,
            cmd: list[str],
            settings: NixSettings = NixSettings(),
            extra_env: dict[str, str] = {}):
        extra_env = extra_env.copy()
        extra_env.update({'NIX_CONFIG': settings.to_config()})
        return self.call(['nix', *cmd], extra_env)

    def eval(
        self, expr: str,
        settings: NixSettings = NixSettings()) -> CommandResult:
        # clone due to reference-shenanigans
        settings = dataclasses.replace(settings).feature('nix-command')

        return self.nix(['eval', '--json', '--expr', expr], settings=settings)
