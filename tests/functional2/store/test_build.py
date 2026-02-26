import os
import stat
from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


@with_files({"config.nix": get_global_asset("config.nix")})
def test_build_dir_permissions(nix: Nix):
    """
    ensure that the build directory parent is not world-accessible
    """

    build_dir = nix.env.dirs.home / "build-dir"
    build_dir.mkdir(0o755)
    fifo = build_dir / "fifo"
    os.mkfifo(fifo)
    expr = f"""
        with import ./config.nix; mkDerivation {{
            name = "test";
            buildCommand = "echo >'{fifo}'; cat '{fifo}' > $out";
        }}
    """

    nix.settings.add_xp_feature("nix-command")
    nix.settings.extra_sandbox_paths = [str(fifo)]

    build = nix.nix(["build", "--build-dir", f"{build_dir}/b", "-E", expr, "--impure"]).start()

    fifo.read_text()
    try:
        child_dir = Path(next(iter((build_dir / "b").iterdir())))
        assert stat.S_IMODE(child_dir.stat().st_mode) in [0o700, 0o710]
    finally:
        fifo.write_text("")

    build.wait().ok()
