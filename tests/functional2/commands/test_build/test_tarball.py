import os
import shutil
from pathlib import Path

import pytest
from _pytest.fixtures import FixtureRequest

from functional2.testlib.fixtures.command import Command
from functional2.testlib.fixtures.file_helper import with_files, Symlink, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset_pack, get_global_asset


TAR_FILES = {
    "tarball": get_global_asset_pack("dependencies")
    | {"default.nix": get_global_asset("dependencies/dependencies.nix")}
}


def _set_mtime_of_folder(path: Path, mtime: int = 1000000000):
    for file in [path] + list(path.iterdir()) if path.is_dir() else []:
        os.utime(file, (mtime, mtime))


@pytest.fixture(params=[("", "cat"), (".xz", "xz"), (".gz", "gzip")])
def tarball(request: FixtureRequest, nix: Nix, files: Path) -> Path:
    ext, compressor = request.param
    # setup
    _set_mtime_of_folder(files / "tarball")
    env = nix.env
    env["GNUTAR_REPRODUCIBLE"] = ""
    tar_exe = shutil.which("tar")
    env.path.add_program("tar")
    tarball_name = f"tarball.tar{ext}"
    tarball_path = files / tarball_name

    # Create tarball
    tarball_content = (
        Command(
            [
                "tar",
                f"--mtime={files / 'tarball' / 'default.nix'}",
                "--owner=0",
                "--group=0",
                "--numeric-owner",
                "--sort=name",
                f"--to-command={compressor}",
                "-c",
                "-f",
                "-",
                "tarball",
            ],
            env,
            exe=tar_exe,
        )
        .run()
        .ok()
        .stdout
    )
    tarball_path.write_bytes(tarball_content)

    res = nix.nix_env(["-f", f"file://{tarball_path}", "-qa", "--out-path"], build=True).run().ok()
    assert "dependencies" in res.stdout_plain

    return tarball_path


@pytest.fixture
def tar_hash(files: Path, nix: Nix) -> str:
    return nix.hash_path(files / "tarball")


@with_files(TAR_FILES)
@pytest.mark.parametrize(
    "flags",
    [
        ["file://{tarball}"],  # noqa: RUF027 # yes, true, but we don't have the variables here
        ["<foo>", "-I", "foo=file://{tarball}"],  # noqa: RUF027
        ["-E", 'import (fetchTarball "file://{tarball}")'],  # noqa: RUF027
        # Do not re-fetch paths already present
        [
            "-E",
            'import (fetchTarball {{ url = "file:///does-not-exist/must-remain-unused/{tarball}"; sha256 = "{tar_hash}"; }})',  # noqa: RUF027
        ],
    ],
)
def test_fetch_tarball(nix: Nix, tarball: Path, tar_hash: str, flags: list[str]):
    # HACK(Commentator2.0): if we don't create a copy, we'd try to format it twice, as it is the same list used in other test calls
    flags = flags[:]

    flags[-1] = flags[-1].format(tarball=tarball, tar_hash=tar_hash)
    nix.nix_build(["-o", "result", *flags]).run().ok()


@with_files(TAR_FILES | {"actual-tmp-dir": {}, "tmp-dir": Symlink("./actual-tmp-dir")})
def test_tarball_symlink_extraction(nix: Nix, files: Path, tarball: Path):
    nix.env.dirs.tmpdir = files / "tmp-dir"

    nix.nix_build(
        ["-o", "result", "-E", f'import (fetchTarball "file://{files / tarball.name}")']
    ).run().ok()

    real_tmp_dir = nix.env.dirs.test_root / "tmp"
    real_tmp_dir.mkdir(exist_ok=True)
    nix.env.dirs.tmpdir = real_tmp_dir

    nix.nix_build(
        [
            "-o",
            "result",
            "--temp-dir",
            f"{files}/tmp-dir",
            "-E",
            f'import (fetchTarball "file://{files / tarball.name}")',
        ]
    ).run().ok()


@with_files(TAR_FILES)
@pytest.mark.parametrize(
    "expr",
    [
        'import (fetchTree "file://{tarball}")',  # noqa: RUF027 # yes, true, but we don't have the variables here
        'import (fetchTree {{ type = "tarball"; url = "file://{tarball}"; }})',  # noqa: RUF027
        'import (fetchTree {{ type = "tarball"; url = "file://{tarball}"; narHash = "{tar_hash}"; }})',  # noqa: RUF027
    ],
)
def test_fetch_tree(nix: Nix, tarball: Path, tar_hash: str, expr: str):
    expr = expr.format(tarball=tarball, tar_hash=tar_hash)
    nix.nix_build(["-o", "result", "-E", expr], flake=True).run().ok()


@with_files(TAR_FILES)
def test_fetch_tree_hash_mismatch(nix: Nix, tarball: Path):
    res = (
        nix.nix_build(
            [
                "-o",
                "result",
                "-E",
                f'import (fetchTree {{ type = "tarball"; url = "file://{tarball}"; narHash = "sha256-xdKv2pq/IiwLSnBBJXW8hNowI4MrdZfW+SYqDQs7Tzc="; }})',
            ],
            flake=True,
        )
        .run()
        .expect(102)
    )
    assert "NAR hash mismatch in input" in res.stderr_plain


@with_files(TAR_FILES)
def test_last_modified(nix: Nix, tarball: Path):
    res = (
        nix.nix(
            ["eval", "--impure", "--expr", f'(fetchTree "file://{tarball}").lastModified'],
            flake=True,
            build=True,
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "1000000000"


@with_files({"config.nix": get_global_asset("config.nix")})
@pytest.mark.parametrize(
    ("flags", "exit_code"),
    [
        (["1 + 2"], 0),
        (["with <fnord/xyzzy>; 1 + 2"], 0),
        (["<fnord/config.nix>", "-I", "fnord=."], 0),
        (["<fnord/xyzzy> 1"], 1),
    ],
)
def test_no_accessing_tar(nix: Nix, flags: list[str], exit_code: int):
    nix.nix_instantiate(
        ["--eval", "-I", "fnord=file://no-such-tarball.tar.gz", "-E", *flags]
    ).run().expect(exit_code)


@with_files(TAR_FILES)
def test_no_submodules(nix: Nix, tarball: Path, tar_hash: str):
    res = (
        nix.nix_instantiate(
            [
                "--strict",
                "--eval",
                "-E",
                f'!((fetchTree {{ type = "tarball"; url = "file://{tarball}"; narHash = "{tar_hash}"; }})) ? submodules',
            ],
            flake=True,
            build=True,
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "true"


@with_files(TAR_FILES)
def test_no_accessing_name(nix: Nix, tarball: Path, tar_hash: str):
    """
    Ensure that the `name` attribute isn't accepted as that would mess with the content-addressing
    """
    res = (
        nix.nix_instantiate(
            [
                "--eval",
                "-E",
                f'fetchTree {{ type = "tarball"; url = "file://{tarball}"; narHash = "{tar_hash}"; name = "foo"; }}',
            ],
            flake=True,
        )
        .run()
        .expect(1)
    )
    assert "error: attribute 'name' isn’t supported in call" in res.stderr_plain  # noqa: RUF001 # for some reason, this error message wants to feel special


@with_files({"bad.tar.xz": CopyFile("assets/test_tarball/bad.tar.xz")})
def test_nix_env_bad_tarball(nix: Nix, files: Path):
    res = nix.nix_env(["-f", f"file://{files / 'bad.tar.xz'}", "-qa", "--out-path"]).run().expect(1)
    assert "error: failed to extract archive (Path contains '..')" in res.stderr_plain
    assert not (nix.env.dirs.tmpdir / "bad").exists()
