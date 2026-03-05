from pathlib import Path
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("simple-drv"))
def test_stdio_forward_failure(nix: Nix, files: Path):
    result = (
        nix.nix_build(
            [
                f"{files}/simple.nix",
                "--builders",
                # build-remote handles the first ssh-ng in-process, but we want to test how nix-daemon
                # behaves in error cases. we need to explicitly set remote-program for the main remote
                # connection since we may choose the wrong binary otherwise. the inner ssh-ng also has
                # a remote-program set to cause predictable failures (rather than using ssh impurely).
                f"ssh-ng://localhost?remote-program={nix.env.dirs.nix_bin_dir}/nix-daemon&remote-store=ssh-ng://localhost?remote-program=false",
                "-j0",
            ]
        )
        .run()
        .expect(1)
    )
    assert "stream ended unexpectedly" in result.stderr_plain
    assert "Lix crashed" not in result.stderr_plain
