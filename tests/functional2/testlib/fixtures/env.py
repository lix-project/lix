import dataclasses
import logging
import os
import platform
import shutil
from pathlib import Path

import pytest

SLASHES_IN_STORE_PATH_UNTIL_PACKAGE = "/nix/store/hash-program_name/".count("/")


logger = logging.getLogger(__name__)


@dataclasses.dataclass
class _ManagedPath:
    """
    Wrapper class to handle building the `PATH` environment variable
    """

    build_shell: dataclasses.InitVar[str]
    """statically linked shell to use within builds which provides coreutils functionality"""
    _path: list[str] = dataclasses.field(default_factory=list)

    def __post_init__(self, build_shell: str):
        self.prepend(build_shell)

    def to_path(self) -> str:
        """
        :return: string to be put into the `PATH` environment variable containing all added paths/programs
        """
        return ":".join(self._path)

    def prepend(self, exec_path: str | Path) -> "_ManagedPath":
        """
        Adds the given file or folder at the FRONT of the path variable
        :param exec_path: executable or folder containing executables to be added
        :return: self, to allow for chaining
        """
        self._path.insert(0, str(exec_path))
        return self

    def append(self, exec_path: str | Path) -> "_ManagedPath":
        """
        Adds the given file or folder at the END of the path variable
        :param exec_path: executable or folder containing executables to be added
        :return: self, to allow for chaining
        """
        self._path.append(str(exec_path))
        return self

    def insert_at(self, exec_path: str | Path, index: int) -> "_ManagedPath":
        """
        Adds the given file or folder at the GIVEN INDEX of the path variable
        :param exec_path: executable or folder containing executable to be added
        :param index: where to insert the path
        :return: self, to allow for chaining
        """
        self._path.insert(index, str(exec_path))
        return self

    def add_program(self, program_name: str, all_associated: bool = True) -> "_ManagedPath":
        """
        Adds the given program to the path by name.
        :param program_name: executable/program to add
        :param all_associated: if True, the folder containing the executable will be added instead. Otherwise, only the provided executable will be added
        :raises ValueError: if the program could not be found
        :return: self, to allow for chaining
        """
        path = shutil.which(program_name)
        if path is None:
            msg = f"Couldn't find program {program_name!r}"
            raise ValueError(msg)
        # Convert to path object for better checking and operations
        path = Path(path)
        if all_associated and not path.is_dir():
            path = path.parent
        # handle as string within the data structure
        path = str(path)
        if path not in self._path:
            self._path.append(path)
        return self

    def remove_path(self, exec_path: str | Path) -> "_ManagedPath":
        """
        Removes the given file or folder from the path
        :param exec_path: file or folder to remove
        :raises ValueError: if the file or folder could not be found
        :return: self, to allow for chaining
        """
        self._path.remove(str(exec_path))
        return self

    def remove_program(self, program_name: str) -> "_ManagedPath":
        """
        Removes the given executable/program from the path by name
        :param program_name: executable/program to remove
        :raises ValueError: if the program was not found or isn't present in path
        :return: self, to allow for chaining
        """
        path = shutil.which(program_name)
        if path is None:
            msg = f"Couldn't find program {program_name!r}"
            raise ValueError(msg)
        if path in self._path or (path := str(Path(path).parent)) in self._path:
            self.remove_path(path)
        else:
            # Mirror behavior of `remove_path`
            msg = f"path.remove({program_name}): {program_name} not in path"
            raise ValueError(msg)
        return self

    def to_sandbox_paths(self) -> list[str]:
        """
        :return: list of strings to put into the `sandbox_paths` nix setting
        """
        ret = []
        for p in self._path:
            if p.startswith("/nix/store/"):
                # adds the entire package to the sandbox,
                # to ensure that dependencies and libraries from within the package are also present
                ret.append("/".join(p.split("/")[:SLASHES_IN_STORE_PATH_UNTIL_PACKAGE]))
            else:
                ret.append(p)
        return ret


@dataclasses.dataclass
class _Dirs:
    test_root: Path | None
    home: Path | None
    nix_log_dir: Path | None
    nix_state_dir: Path | None
    nix_conf_dir: Path | None
    nix_bin_dir: Path | None
    nix_store_dir: Path | None
    cache_dir: Path | None
    xdg_cache_home: Path | None
    """used for nar caching"""

    def get_env_keys(self) -> set[str]:
        return {f.name.upper() for f in dataclasses.fields(self)}

    def to_env_vars(self) -> dict[str, str]:
        return {k.upper(): v for k, v in dataclasses.asdict(self).items() if v is not None}


class ManagedEnv:
    def __init__(self, tmp_path: Path):
        # Things fetched from the global env
        build_shell = os.environ.get("BUILD_TEST_SHELL")
        global_path = os.environ.get("PATH")
        # `NIX_BIN_DIR` either propagated from us or set by meson
        # Set to the codebase internal output if started standalone
        # This is where the current lix binaries are located.
        # local import to avoid cyclic dependencies
        from functional2.testlib.utils import lix_base_folder  # noqa: PLC0415

        lix_bin = Path(os.environ.get("NIX_BIN_DIR", lix_base_folder / "outputs/out/bin"))

        self._env = {}
        self.path = _ManagedPath(build_shell)
        self._tmp_path = tmp_path
        self.shell_dir = build_shell or "/bin"

        self.dirs = _Dirs(
            test_root=self._get_dir(""),
            home=self._get_dir("test-home"),
            nix_log_dir=self._get_dir("var/log/nix"),
            nix_state_dir=self._get_dir("var/nix"),
            nix_conf_dir=self._get_dir("etc/nix"),
            nix_bin_dir=lix_bin,
            nix_store_dir=self._get_dir("nix/store"),
            cache_dir=self._get_dir("binary-cache"),
            xdg_cache_home=self._get_dir("test-home/.cache"),
        )
        self.path.prepend(self.dirs.nix_bin_dir)
        self.init_defaults(global_path)

    def _get_dir(self, sub_path: str) -> Path:
        p = self._tmp_path / sub_path
        p.mkdir(parents=True, exist_ok=True)
        return p

    def init_defaults(self, global_path: str):
        self._env = {
            # Do not use the system-wide or local config for git, but *none* instead
            "GIT_CONFIG_SYSTEM": "/dev/null",
            # Shell to use, required by lix to run sub processes / commands
            "SHELL": f"{self.shell_dir}/sh",
            # when writing things to the terminal (esp with man pages) use cat, to print the full output to stdout
            "PAGER": "cat",
            "BUILD_TEST_SHELL": self.shell_dir,
        }
        if platform.system() == "Darwin":
            # Darwin / Apple behaves differently and requires _NIX_TEST_NO_SANDBOX to be set for whatever reason
            self._env |= {"_NIX_TEST_NO_SANDBOX": "1"}
            # copy global path to maintain features usually provided by busybox
            [self.path.append(p) for p in global_path.split(":")]

    def set_env(self, name: str, value: str):
        if name in self.dirs.get_env_keys():
            msg = f"Overriding paths should be done using the `env.dirs` attribute, use `env.dirs.{name.lower()}` instead."
            raise ValueError(msg)
        if name == "PATH":
            msg = "Setting of path not supported. use `env.path` instead"
            raise ValueError(msg)
        if value is None:
            msg = "setting to `None` is not allowed. did you mean to use `env.unset_env`?"
            raise ValueError(msg)
        self._env[name] = value

    def __setitem__(self, key: str, value: str) -> None:
        return self.set_env(key, value)

    def get_env(self, name: str, default: str | Path | None = None) -> str | Path | None:
        if name in self.dirs.get_env_keys():
            return getattr(self.dirs, name.lower())
        if name == "PATH":
            msg = "getting of path not supported, use `env.path` instead"
            raise ValueError(msg)
        return self._env.get(name, default)

    def __getitem__(self, item: str) -> str | Path | None:
        itm = self.get_env(item)
        if itm is None:
            msg = f"{itm} is not set"
            raise KeyError(msg)
        return itm

    def unset_env(self, name: str) -> str | None:
        if name in self.dirs.get_env_keys():
            msg = f"Overriding paths should be done using the `env.dirs` attribute, use `env.dirs.{name.lower()}` instead."
            raise ValueError(msg)
        return self._env.pop(name, None)

    def __delitem__(self, key: str) -> str | None:
        itm = self.unset_env(key)
        if itm is None:
            msg = f"{itm} is not set"
            raise KeyError(msg)
        return itm

    def to_env(self) -> dict[str, str]:
        ret = self.dirs.to_env_vars()
        ret["PATH"] = self.path.to_path()
        for k, v in self._env.copy().items():
            if v is not None:
                ret[k] = v
            else:
                logger.warning("environment variable '%s' is none", k)
        return ret


@pytest.fixture
def env(tmp_path: Path) -> ManagedEnv:
    return ManagedEnv(tmp_path)
